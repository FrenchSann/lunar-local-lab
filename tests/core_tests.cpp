#include "lunar/core.hpp"
#include <atomic>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
using namespace lunar;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #x); } while(false)
Snapshot sample() {
    Snapshot s;
    s.context = {1, "127.0.0.1", true, true, true, false};
    s.captured = Time{Millis{1000}}; s.revision = 1;
    s.equipped = {1, 0, 2, 0, 90, 100};
    return s;
}
void start(ActionCoordinator& c, const Snapshot& s) { CHECK(c.tick(s).empty()); CHECK(c.arm(s.context)); }
void contextTest() {
    ActionCoordinator c; auto s = sample(); start(c, s);
    s.physicalLeft = true; auto a = c.tick(s); CHECK(a.size() == 1); CHECK(c.revalidate(a[0], s));
    auto newer = s; ++newer.revision; CHECK(!c.revalidate(a[0], newer));
    ++s.context.generation; CHECK(c.tick(s).empty()); CHECK(!c.revalidate(a[0], s)); CHECK(c.tick(s).empty());
    CHECK(c.arm(s.context));
    s.block = true; a = c.tick(s); CHECK(a[0].kind == ActionKind::BeginMining);
    s.context.peer = "192.168.0.1"; a = c.tick(s); CHECK(a.size() == 1 && a[0].kind == ActionKind::EndMining);
    CHECK(!c.revalidate(a[0], s)); CHECK(!c.arm(s.context));
    for (const auto* peer : {"localhost", "127.0.0.1.evil", "::ffff:127.0.0.1", "8.8.8.8"}) { s.context.peer = peer; CHECK(!s.context.eligible()); }
    s.context.peer = "::1"; CHECK(s.context.eligible());
    s.context.direct = false; CHECK(!s.context.eligible()); s.context.direct = true;
    s.context.menu = true; CHECK(!s.context.eligible()); s.context.menu = false;
    s.context.focused = false; CHECK(!s.context.eligible()); s.context.focused = true;
    s.context.world = false; CHECK(!s.context.eligible());
    CHECK(c.stop().empty()); CHECK(!c.arm(sample().context)); CHECK(c.tick(sample()).empty());
}
void schedulerTest() {
    ActionCoordinator c; auto s = sample(); start(c, s);
    s.syntheticLeft = true; CHECK(c.tick(s).empty());
    s.physicalLeft = true; CHECK(c.tick(s).size() == 1); CHECK(c.tick(s).empty());
    s.captured += Millis{99}; CHECK(c.tick(s).empty());
    s.captured += Millis{1}; CHECK(c.tick(s).size() == 1);
    s.captured += Millis{10000}; CHECK(c.tick(s).size() == 1); CHECK(c.tick(s).empty());
    s.block = true; auto a = c.tick(s); CHECK(a.size() == 1 && a[0].kind == ActionKind::BeginMining);
    s.captured += Millis{500}; CHECK(c.tick(s).empty());
    s.physicalLeft = false; a = c.tick(s); CHECK(a.size() == 1 && a[0].kind == ActionKind::EndMining);
    s.physicalLeft = true; s.block = false; CHECK(c.tick(s).size() == 1);
    s.captured -= Millis{1}; CHECK(c.tick(s).empty()); CHECK(c.tick(s).empty());
    CHECK(c.arm(s.context)); s.block = true; CHECK(c.tick(s).size() == 1);
    a = c.stop(); CHECK(a.size() == 1 && a[0].kind == ActionKind::EndMining); CHECK(c.stop().empty());
    Configuration bad; bad.repeat = Millis{0}; bool threw = false;
    try { ActionCoordinator invalid(bad); } catch(const std::invalid_argument&) { threw = true; } CHECK(threw);
}
void armorTest() {
    ActionCoordinator c; auto s = sample(); start(c, s);
    s.equipped.remaining = 10; s.inventoryReady = true;
    s.inventory = {{2,0,3,1,60,100}, {3,0,3,2,50,100}, {4,1,10,10,100,100}, {5,0,9,9,0,100}};
    CHECK(replacement(s.equipped,s.inventory,.15)->id == 3);
    s.cursorEmpty = false; CHECK(c.tick(s).empty()); s.cursorEmpty = true;
    s.physicalLeft = true; s.block = true; CHECK(c.tick(s)[0].kind == ActionKind::BeginMining);
    CHECK(c.transaction() == TransactionState::Idle); s.block = false;
    auto a = c.tick(s); CHECK(a.size() == 2 && a[1].kind == ActionKind::Equip); CHECK(c.revalidate(a[1],s));
    auto changed = s; changed.cursorEmpty = false; CHECK(!c.revalidate(a[1],changed));
    CHECK(c.tick(s).empty()); s.captured += Millis{500}; CHECK(c.tick(s).empty());
    CHECK(c.transaction() == TransactionState::Reobserve); CHECK(!c.revalidate(a[1],s));
    s.captured += Millis{5000}; CHECK(c.tick(s).empty());
    // Late confirmation is observed before a retry, so no duplicate equipment action.
    s.equipped = s.inventory[1]; ++s.revision; a = c.tick(s);
    CHECK(c.transaction() == TransactionState::Idle); CHECK(a.size() == 1 && a[0].kind == ActionKind::Click);
    s.equipped.remaining = 1; s.inventory = {{8,0,4,0,90,100}}; ++s.revision;
    a = c.tick(s); CHECK(a[0].kind == ActionKind::Equip);
    s.equipped = s.inventory[0]; ++s.revision; c.tick(s); CHECK(c.transaction() == TransactionState::Idle);
    s.equipped.remaining = 15; CHECK(!replacement(s.equipped,s.inventory,.15));
    s.equipped.maximum = 0; CHECK(!replacement(s.equipped,s.inventory,.15));
    s.equipped = {1,0,1,0,1,100}; ++s.revision; c.tick(s);
    ++s.context.generation; CHECK(c.tick(s).empty()); CHECK(c.transaction() == TransactionState::Idle);
}
void geometryTest() {
    const Box b{{2,-1,-1},{4,1,1}};
    CHECK(segmentEntry({0,0,0},{4,0,0},b) == .5);
    CHECK(segmentEntry({3,0,0},{3,0,0},b) == 0);
    CHECK(!segmentEntry({0,2,0},{4,2,0},b));
    CHECK(segmentEntry({5,0,0},{1,0,0},b) == .25);
    CHECK(segmentEntry({0,1,0},{2,1,0},b) == 1);
    const std::vector<Target> targets{{10,b}, {9,b}, {8,{{5,-1,-1},{6,1,1}}}};
    auto hit = selectTarget({0,0,0},{5,0,0},3,targets,{});
    CHECK(hit && hit->id == 9 && hit->entryDistance == 2);
    CHECK(!selectTarget({0,0,0},{1,0,0},1,targets,{}));
    CHECK(!selectTarget({0,0,0},{1,0,0},6,targets,{{{1,-1,-1},{1.5,1,1}}}));
    CHECK(!selectTarget({0,0,0},{1,0,0},6,targets,{b}));
    CHECK(!selectTarget({0,0,0},{0,0,0},6,targets,{}));
    CHECK(!selectTarget({0,0,0},{1,0,0},std::numeric_limits<double>::quiet_NaN(),targets,{}));
    CHECK(!selectTarget({0,0,0},{1,0,0},6,targets,{{{2,0,0},{1,0,0}}}));
    CHECK(!segmentEntry({0,0,0},{4,0,0},{{2,0,0},{1,0,0}}));
}
void runtimeTest() {
    NativeRuntime r; std::promise<void> entered, release; auto releaseFuture = release.get_future();
    auto worker = std::async(std::launch::async,[&] { CallbackLease lease(r); if (!lease) throw std::runtime_error("not admitted"); entered.set_value(); releaseFuture.wait(); });
    entered.get_future().wait(); r.requestStop(); CHECK(!r.enter());
    auto waiter = std::async(std::launch::async,[&] { r.waitQuiescent(); });
    const auto state = waiter.wait_for(Millis{20}); release.set_value(); worker.get(); waiter.get();
    CHECK(state == std::future_status::timeout); CHECK(!r.enter()); r.requestStop(); r.waitQuiescent();
    NativeRuntime empty;
    auto earlyWaiter = std::async(std::launch::async,[&] { empty.waitQuiescent(); });
    CHECK(earlyWaiter.wait_for(Millis{20}) == std::future_status::timeout);
    empty.requestStop(); earlyWaiter.get();
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Expected test case");
        const std::string name = argv[1];
        if(name=="context") contextTest(); else if(name=="scheduler") schedulerTest();
        else if(name=="armor") armorTest(); else if(name=="geometry") geometryTest();
        else if(name=="runtime") runtimeTest(); else throw std::runtime_error("Unknown test");
        std::cout << name << ": " << checks << " checks passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
