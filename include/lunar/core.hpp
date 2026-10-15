#pragma once
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace lunar {
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
using Millis = std::chrono::milliseconds;
struct Configuration {
    Millis repeat{100}, transactionTimeout{500};
    double armorThreshold{0.15}, reach{3.0};
    void validate() const;
};
// Optional observed network identity for context invalidation, not an activation restriction.
struct Context {
    std::uint64_t generation{};
    std::string peer;
    bool direct{}, world{}, focused{}, menu{};
    bool operator==(const Context&) const = default;
    bool eligible() const;
};
struct Armor {
    int id{}, slot{}, protection{}, attributes{}, remaining{}, maximum{};
    bool usable() const;
    double fraction() const;
};
struct Snapshot {
    Context context;
    Time captured{};
    std::uint64_t revision{};
    bool physicalLeft{}, syntheticLeft{}, block{}, inventoryReady{}, cursorEmpty{true};
    Armor equipped;
    std::vector<Armor> inventory;
};
enum class ActionKind { Click, BeginMining, EndMining, Equip };
struct Action {
    ActionKind kind;
    Context context;
    std::uint64_t revision{};
    int item{};
};
enum class TransactionState { Idle, AwaitingConfirmation, Reobserve };
std::optional<Armor> replacement(const Armor&, const std::vector<Armor>&, double threshold);

// Runs synchronously on the future application's verified execution thread.
// Actions are intents, not OS input or Minecraft packets.
class ActionCoordinator {
public:
    explicit ActionCoordinator(Configuration config = {});
    std::vector<Action> tick(const Snapshot&);
    bool arm(const Context&);
    bool revalidate(const Action&, const Snapshot&) const;
    std::vector<Action> stop();
    TransactionState transaction() const { return transaction_; }
private:
    Configuration config_;
    std::optional<Context> context_;
    bool armed_{}, mining_{}, stopped_{};
    std::optional<Time> last_, next_;
    TransactionState transaction_{TransactionState::Idle};
    Time deadline_{};
    std::uint64_t transactionRevision_{}, reobserveRevision_{};
    int desired_{};
    void cancel(std::vector<Action>&);
};

using Vec3 = std::array<double, 3>;
struct Box { Vec3 min, max; };
struct Target { int id; Box box; };
struct Selection { int id; double entryDistance; };
std::optional<double> segmentEntry(Vec3 from, Vec3 to, const Box&);
std::optional<Selection> selectTarget(Vec3 eye, Vec3 direction, double reach,
    const std::vector<Target>&, const std::vector<Box>& obstacles);

// Host must unregister callbacks and join owned workers before actual DLL unload.
// This barrier protects registered in-flight callbacks, not unknown host callbacks.
class NativeRuntime {
public:
    bool enter();
    void leave();
    void requestStop();
    void waitQuiescent(); // Must not be called by an admitted callback.
private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_{};
    unsigned active_{};
};
class CallbackLease {
public:
    explicit CallbackLease(NativeRuntime& runtime) : runtime_(runtime.enter() ? &runtime : nullptr) {}
    ~CallbackLease() { if (runtime_) runtime_->leave(); }
    CallbackLease(const CallbackLease&) = delete;
    CallbackLease& operator=(const CallbackLease&) = delete;
    explicit operator bool() const { return runtime_ != nullptr; }
private:
    NativeRuntime* runtime_;
};
}
