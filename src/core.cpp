#include "lunar/core.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace lunar {
void Configuration::validate() const {
    if (repeat < Millis{1} || repeat > Millis{60000} || transactionTimeout < Millis{1}
        || transactionTimeout > Millis{60000} || !std::isfinite(armorThreshold)
        || armorThreshold < 0 || armorThreshold > 1 || !std::isfinite(reach) || reach <= 0 || reach > 64)
        throw std::invalid_argument("Invalid lab configuration");
}
bool Context::eligible() const {
    // Deliberately conservative subset of numeric loopback addresses. No DNS.
    return direct && world && focused && !menu && (peer == "127.0.0.1" || peer == "::1");
}
bool Armor::usable() const { return id > 0 && slot >= 0 && slot < 4 && maximum > 0 && remaining > 0 && remaining <= maximum && protection >= 0 && attributes >= 0; }
double Armor::fraction() const { return maximum > 0 ? static_cast<double>(remaining) / maximum : 0; }
std::optional<Armor> replacement(const Armor& current, const std::vector<Armor>& candidates, double threshold) {
    if (!std::isfinite(threshold) || threshold < 0 || threshold > 1 || current.maximum <= 0
        || current.remaining < 0 || current.remaining > current.maximum || current.fraction() >= threshold) return {};
    std::optional<Armor> best;
    for (const auto& a : candidates) {
        if (!a.usable() || a.slot != current.slot || a.id == current.id || a.fraction() <= threshold) continue;
        // Explicit simulation policy: protection, attribute score, durability, stable ID.
        const auto rank = [](const Armor& x) { return std::tuple{x.protection, x.attributes, x.fraction(), x.remaining, -x.id}; };
        if (!best || rank(a) > rank(*best)) best = a;
    }
    return best;
}
ActionCoordinator::ActionCoordinator(Configuration config) : config_(config) { config_.validate(); }
void ActionCoordinator::cancel(std::vector<Action>& out) {
    if (mining_ && context_) out.push_back({ActionKind::EndMining, *context_, 0, 0});
    mining_ = false; armed_ = false; next_.reset(); transaction_ = TransactionState::Idle;
}
bool ActionCoordinator::arm(const Context& context) {
    if (stopped_ || !context_ || *context_ != context || !context.eligible()) return false;
    armed_ = true; return true;
}
std::vector<Action> ActionCoordinator::tick(const Snapshot& s) {
    std::vector<Action> out;
    if (stopped_) return out;
    if (!context_ || *context_ != s.context || (last_ && s.captured < *last_)) {
        cancel(out); context_ = s.context; last_ = s.captured; return out;
    }
    last_ = s.captured;
    if (!armed_ || !s.context.eligible()) { cancel(out); return out; }
    const auto emit = [&](ActionKind k, int id = 0) { out.push_back({k, s.context, s.revision, id}); };
    // Reobserve inventory after expiry before any retry, even if no input is held.
    if (transaction_ == TransactionState::AwaitingConfirmation) {
        if (s.revision > transactionRevision_ && s.equipped.id == desired_) transaction_ = TransactionState::Idle;
        else if (s.captured >= deadline_) { transaction_ = TransactionState::Reobserve; reobserveRevision_ = s.revision; }
    } else if (transaction_ == TransactionState::Reobserve && s.revision > reobserveRevision_ && s.inventoryReady && s.cursorEmpty) {
        transaction_ = TransactionState::Idle;
    }
    // Physical hold on a block owns the action channel, including during a pending equipment transaction.
    if (s.physicalLeft && s.block) {
        if (!mining_) { emit(ActionKind::BeginMining); mining_ = true; }
        next_.reset(); return out;
    }
    if (mining_) { emit(ActionKind::EndMining); mining_ = false; next_.reset(); }
    if (transaction_ != TransactionState::Idle) return out;
    if (s.inventoryReady && s.cursorEmpty) {
        if (const auto item = replacement(s.equipped, s.inventory, config_.armorThreshold)) {
            desired_ = item->id; transactionRevision_ = s.revision;
            deadline_ = s.captured + config_.transactionTimeout;
            transaction_ = TransactionState::AwaitingConfirmation;
            next_.reset(); emit(ActionKind::Equip, desired_); return out;
        }
    }
    if (!s.physicalLeft) { next_.reset(); return out; }
    if (!next_ || s.captured >= *next_) {
        emit(ActionKind::Click); next_ = s.captured + config_.repeat;
    }
    return out;
}
bool ActionCoordinator::revalidate(const Action& a, const Snapshot& s) const {
    // EndMining is a release of a previously owned lease, never a write into a new context.
    if (a.kind == ActionKind::EndMining) return context_ && a.context == s.context;
    if (stopped_ || !armed_ || !context_ || *context_ != s.context || a.context != s.context
        || !s.context.eligible() || a.revision != s.revision) return false;
    if (a.kind == ActionKind::Click) return s.physicalLeft && !s.block && transaction_ == TransactionState::Idle;
    if (a.kind == ActionKind::BeginMining) return s.physicalLeft && s.block;
    if (!s.inventoryReady || !s.cursorEmpty || (s.physicalLeft && s.block)
        || transaction_ != TransactionState::AwaitingConfirmation || a.item != desired_ || s.captured >= deadline_) return false;
    const auto item = replacement(s.equipped, s.inventory, config_.armorThreshold);
    return item && item->id == a.item;
}
std::vector<Action> ActionCoordinator::stop() { std::vector<Action> out; cancel(out); stopped_ = true; return out; }

std::optional<double> segmentEntry(Vec3 from, Vec3 to, const Box& b) {
    double low = 0, high = 1;
    for (unsigned i = 0; i < 3; ++i) {
        if (!std::isfinite(from[i]) || !std::isfinite(to[i]) || !std::isfinite(b.min[i]) || !std::isfinite(b.max[i]) || b.min[i] > b.max[i]) return {};
        const double d = to[i] - from[i];
        if (!std::isfinite(d)) return {};
        if (d == 0) { if (from[i] < b.min[i] || from[i] > b.max[i]) return {}; continue; }
        double a = (b.min[i] - from[i]) / d, c = (b.max[i] - from[i]) / d;
        if (a > c) std::swap(a, c);
        low = std::max(low, a); high = std::min(high, c);
        if (low > high) return {};
    }
    return low;
}
std::optional<Selection> selectTarget(Vec3 eye, Vec3 direction, double reach,
    const std::vector<Target>& targets, const std::vector<Box>& obstacles) {
    if (!std::isfinite(reach) || reach <= 0 || reach > 64) return {};
    const double norm = std::hypot(direction[0], direction[1], direction[2]);
    if (!std::isfinite(norm) || norm == 0) return {};
    Vec3 end{};
    for (unsigned i = 0; i < 3; ++i) { if (!std::isfinite(eye[i])) return {}; end[i] = eye[i] + direction[i] / norm * reach; }
    double barrier = 2;
    for (const auto& b : obstacles) {
        // Invalid obstacle geometry fails closed, since visibility cannot be established.
        for (unsigned i = 0; i < 3; ++i) if (!std::isfinite(b.min[i]) || !std::isfinite(b.max[i]) || b.min[i] > b.max[i]) return {};
        if (auto hit = segmentEntry(eye, end, b)) barrier = std::min(barrier, *hit);
    }
    std::optional<Selection> best;
    for (const auto& t : targets) {
        const auto hit = segmentEntry(eye, end, t.box);
        if (!hit || *hit >= barrier) continue;
        const double distance = *hit * reach;
        if (!best || distance < best->entryDistance || (distance == best->entryDistance && t.id < best->id)) best = Selection{t.id, distance};
    }
    return best;
}
bool NativeRuntime::enter() { std::lock_guard lock(mutex_); if (stopping_) return false; ++active_; return true; }
void NativeRuntime::leave() { std::lock_guard lock(mutex_); if (!active_) std::terminate(); if (--active_ == 0) cv_.notify_all(); }
void NativeRuntime::requestStop() { std::lock_guard lock(mutex_); stopping_ = true; cv_.notify_all(); }
void NativeRuntime::waitQuiescent() { std::unique_lock lock(mutex_); cv_.wait(lock, [&] { return stopping_ && active_ == 0; }); }
}
