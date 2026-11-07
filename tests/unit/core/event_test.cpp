/// @file event_test.cpp
/// @brief Comprehensive unit tests for eng::EventDispatcher and eng::EventQueue<Event>.

#include <array>
#include <core/event.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

#define TEST_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

struct PlayerSpawnEvent {
    int playerId{0};
};

struct DamageEvent {
    int damage{0};
};

struct HealEvent {
    int amount{0};
};

struct TrackedObject {
    static inline int sConstructed{0};
    static inline int sDestructed{0};

    int value{0};

    static void reset() noexcept {
        sConstructed = 0;
        sDestructed = 0;
    }

    static int aliveCount() noexcept { return sConstructed - sDestructed; }

    explicit TrackedObject(int v) noexcept : value(v) { ++sConstructed; }

    ~TrackedObject() noexcept { ++sDestructed; }

    TrackedObject(const TrackedObject& other) noexcept : value(other.value) { ++sConstructed; }

    TrackedObject& operator=(const TrackedObject& other) noexcept = default;

    TrackedObject(TrackedObject&& other) noexcept : value(other.value) { ++sConstructed; }

    TrackedObject& operator=(TrackedObject&& other) noexcept = default;
};

struct NonTrivialEvent {
    TrackedObject payload;

    explicit NonTrivialEvent(int v) noexcept : payload(v) {}
};

class HealthComponent {
public:
    int health{100};

    void onDamage(const DamageEvent& evt) noexcept { health -= evt.damage; }
};

int testTypedDispatch() {
    alignas(eng::EventDispatcher::requiredAlignment())
        std::array<std::byte, eng::EventDispatcher::requiredBytes(8)>
            dispMem{};
    eng::EventDispatcher dispatcher(dispMem, 8);

    int spawnCount = 0;
    int lastPlayerId = 0;
    int damageCount = 0;
    int lastDamage = 0;

    auto onSpawn = [](const PlayerSpawnEvent& e, void* ctx) {
        auto* pair = static_cast<std::pair<int*, int*>*>(ctx);
        ++(*pair->first);
        *pair->second = e.playerId;
    };

    auto onDamage = [](const DamageEvent& e, void* ctx) {
        auto* pair = static_cast<std::pair<int*, int*>*>(ctx);
        ++(*pair->first);
        *pair->second = e.damage;
    };

    std::pair<int*, int*> spawnCtx{&spawnCount, &lastPlayerId};
    std::pair<int*, int*> damageCtx{&damageCount, &lastDamage};

    const auto t1 = dispatcher.registerListener<PlayerSpawnEvent>(onSpawn, &spawnCtx);
    const auto t2 = dispatcher.registerListener<DamageEvent>(onDamage, &damageCtx);

    TEST_CHECK(t1.isValid());
    TEST_CHECK(t2.isValid());
    TEST_CHECK(dispatcher.size() == 2);
    TEST_CHECK(dispatcher.listenerCount<PlayerSpawnEvent>() == 1);
    TEST_CHECK(dispatcher.listenerCount<DamageEvent>() == 1);

    // Dispatch PlayerSpawnEvent: only spawn listener should be invoked
    size_t invoked = dispatcher.dispatch(PlayerSpawnEvent{.playerId = 42});
    TEST_CHECK(invoked == 1);
    TEST_CHECK(spawnCount == 1);
    TEST_CHECK(lastPlayerId == 42);
    TEST_CHECK(damageCount == 0);

    // Dispatch DamageEvent: only damage listener should be invoked
    invoked = dispatcher.dispatch(DamageEvent{.damage = 25});
    TEST_CHECK(invoked == 1);
    TEST_CHECK(spawnCount == 1);
    TEST_CHECK(damageCount == 1);
    TEST_CHECK(lastDamage == 25);

    return 0;
}

int testMultipleListeners() {
    alignas(eng::EventDispatcher::requiredAlignment())
        std::array<std::byte, eng::EventDispatcher::requiredBytes(8)>
            dispMem{};
    eng::EventDispatcher dispatcher(dispMem, 8);

    int callMask = 0;

    auto l1 = [](const DamageEvent&, void* ctx) { *static_cast<int*>(ctx) |= 1; };
    auto l2 = [](const DamageEvent&, void* ctx) { *static_cast<int*>(ctx) |= 2; };
    auto l3 = [](const DamageEvent&, void* ctx) { *static_cast<int*>(ctx) |= 4; };

    const auto t1 = dispatcher.registerListener<DamageEvent>(l1, &callMask);
    const auto t2 = dispatcher.registerListener<DamageEvent>(l2, &callMask);
    const auto t3 = dispatcher.registerListener<DamageEvent>(l3, &callMask);

    TEST_CHECK(t1.isValid());
    TEST_CHECK(t2.isValid());
    TEST_CHECK(t3.isValid());
    TEST_CHECK(dispatcher.listenerCount<DamageEvent>() == 3);

    const size_t count = dispatcher.dispatch(DamageEvent{.damage = 10});
    TEST_CHECK(count == 3);
    TEST_CHECK(callMask == 7);

    return 0;
}

int testListenerRegistrationAndRemoval() {
    alignas(eng::EventDispatcher::requiredAlignment())
        std::array<std::byte, eng::EventDispatcher::requiredBytes(8)>
            dispMem{};
    eng::EventDispatcher dispatcher(dispMem, 8);

    int callCount = 0;
    auto listener = [](const DamageEvent&, void* ctx) { ++(*static_cast<int*>(ctx)); };

    const auto token = dispatcher.registerListener<DamageEvent>(listener, &callCount);
    TEST_CHECK(token.isValid());
    TEST_CHECK(dispatcher.size() == 1);
    TEST_CHECK(dispatcher.listenerCount<DamageEvent>() == 1);

    dispatcher.dispatch(DamageEvent{.damage = 5});
    TEST_CHECK(callCount == 1);

    // Unregister listener
    TEST_CHECK(dispatcher.unregisterListener(token));
    TEST_CHECK(dispatcher.size() == 0);
    TEST_CHECK(dispatcher.listenerCount<DamageEvent>() == 0);

    // Dispatch again: nothing should be called
    const size_t invoked = dispatcher.dispatch(DamageEvent{.damage = 5});
    TEST_CHECK(invoked == 0);
    TEST_CHECK(callCount == 1);

    return 0;
}

int testInvalidAndStaleTokens() {
    alignas(eng::EventDispatcher::requiredAlignment())
        std::array<std::byte, eng::EventDispatcher::requiredBytes(4)>
            dispMem{};
    eng::EventDispatcher dispatcher(dispMem, 4);

    // Invalid tokens
    TEST_CHECK(!dispatcher.unregisterListener(eng::ListenerToken{}));
    TEST_CHECK(!dispatcher.unregisterListener(eng::ListenerToken::invalid()));
    TEST_CHECK(!dispatcher.unregisterListener(eng::ListenerToken{99999}));

    auto dummy = [](const DamageEvent&) {};
    const auto tA = dispatcher.registerListener<DamageEvent>(dummy);
    TEST_CHECK(tA.isValid());

    // First unregister succeeds
    TEST_CHECK(dispatcher.unregisterListener(tA));

    // Repeated unregister on already-unregistered token fails safely
    TEST_CHECK(!dispatcher.unregisterListener(tA));

    // Stale token verification across slot reuse:
    // Register B, reusing slot 0 with incremented generation
    const auto tB = dispatcher.registerListener<DamageEvent>(dummy);
    TEST_CHECK(tB.isValid());
    TEST_CHECK(tB != tA); // Generations differ

    // Unregistering using old token tA must fail (stale token check)
    TEST_CHECK(!dispatcher.unregisterListener(tA));
    TEST_CHECK(dispatcher.listenerCount<DamageEvent>() == 1);

    // Unregistering using tB succeeds
    TEST_CHECK(dispatcher.unregisterListener(tB));
    TEST_CHECK(dispatcher.listenerCount<DamageEvent>() == 0);

    return 0;
}

int testDispatchWithoutListeners() {
    alignas(eng::EventDispatcher::requiredAlignment())
        std::array<std::byte, eng::EventDispatcher::requiredBytes(4)>
            dispMem{};
    eng::EventDispatcher dispatcher(dispMem, 4);

    // Dispatching an event type with zero listeners returns 0 safely
    const size_t invoked = dispatcher.dispatch(HealEvent{.amount = 50});
    TEST_CHECK(invoked == 0);

    return 0;
}

int testMemberFunctionListener() {
    alignas(eng::EventDispatcher::requiredAlignment())
        std::array<std::byte, eng::EventDispatcher::requiredBytes(4)>
            dispMem{};
    eng::EventDispatcher dispatcher(dispMem, 4);

    HealthComponent comp;
    TEST_CHECK(comp.health == 100);

    const auto token =
        dispatcher.registerListener<DamageEvent, HealthComponent, &HealthComponent::onDamage>(
            &comp);
    TEST_CHECK(token.isValid());

    dispatcher.dispatch(DamageEvent{.damage = 30});
    TEST_CHECK(comp.health == 70);

    TEST_CHECK(dispatcher.unregisterListener(token));
    dispatcher.dispatch(DamageEvent{.damage = 30});
    TEST_CHECK(comp.health == 70); // Unregistered, not invoked

    return 0;
}

int testListenerRemovalDuringDispatch() {
    alignas(eng::EventDispatcher::requiredAlignment())
        std::array<std::byte, eng::EventDispatcher::requiredBytes(8)>
            dispMem{};
    eng::EventDispatcher dispatcher(dispMem, 8);

    struct Context {
        eng::EventDispatcher* disp{nullptr};
        eng::ListenerToken token2;
        int l1Calls{0};
        int l2Calls{0};
    };

    Context ctx;
    ctx.disp = &dispatcher;

    auto l1 = [](const DamageEvent&, void* userCtx) {
        auto* c = static_cast<Context*>(userCtx);
        ++c->l1Calls;
        // Unregister listener 2 during listener 1's invocation
        c->disp->unregisterListener(c->token2);
    };

    auto l2 = [](const DamageEvent&, void* userCtx) {
        auto* c = static_cast<Context*>(userCtx);
        ++c->l2Calls;
    };

    const auto t1 = dispatcher.registerListener<DamageEvent>(l1, &ctx);
    ctx.token2 = dispatcher.registerListener<DamageEvent>(l2, &ctx);

    TEST_CHECK(t1.isValid());
    TEST_CHECK(ctx.token2.isValid());
    TEST_CHECK(dispatcher.size() == 2);

    // Dispatch: listener 1 executes and unregisters listener 2; listener 2 must not be called
    const size_t count = dispatcher.dispatch(DamageEvent{.damage = 10});
    TEST_CHECK(count == 1);
    TEST_CHECK(ctx.l1Calls == 1);
    TEST_CHECK(ctx.l2Calls == 0);

    // After dispatch unwinds, pending removals are purged
    TEST_CHECK(dispatcher.size() == 1);
    TEST_CHECK(dispatcher.listenerCount<DamageEvent>() == 1);

    return 0;
}

int testListenerRegistrationDuringDispatch() {
    alignas(eng::EventDispatcher::requiredAlignment())
        std::array<std::byte, eng::EventDispatcher::requiredBytes(8)>
            dispMem{};
    eng::EventDispatcher dispatcher(dispMem, 8);

    struct Context {
        eng::EventDispatcher* disp{nullptr};
        int l1Calls{0};
        int l2Calls{0};
        bool innerRegistered{false};
    };

    Context ctx;
    ctx.disp = &dispatcher;

    auto l1 = [](const DamageEvent&, void* userCtx) {
        auto* c = static_cast<Context*>(userCtx);
        ++c->l1Calls;
        // Register listener 2 during active dispatch
        auto fn = [](const DamageEvent&, void* p) {
            auto* inner = static_cast<Context*>(p);
            ++inner->l2Calls;
        };
        const auto innerToken = c->disp->registerListener<DamageEvent>(fn, c);
        if (innerToken.isValid()) {
            c->innerRegistered = true;
        }
    };

    const auto tMain = dispatcher.registerListener<DamageEvent>(l1, &ctx);
    TEST_CHECK(tMain.isValid());

    // First dispatch: listener 1 is called, registers listener 2.
    // Listener 2 must NOT be called in this first dispatch!
    size_t count = dispatcher.dispatch(DamageEvent{.damage = 5});
    TEST_CHECK(count == 1);
    TEST_CHECK(ctx.l1Calls == 1);
    TEST_CHECK(ctx.l2Calls == 0);
    TEST_CHECK(ctx.innerRegistered);

    // Second dispatch: both listener 1 and listener 2 are now active
    count = dispatcher.dispatch(DamageEvent{.damage = 5});
    TEST_CHECK(count == 2);
    TEST_CHECK(ctx.l1Calls == 2);
    TEST_CHECK(ctx.l2Calls == 1);

    return 0;
}

int testQueueFifoOrdering() {
    alignas(eng::EventQueue<DamageEvent>::requiredAlignment())
        std::array<std::byte, eng::EventQueue<DamageEvent>::requiredBytes(8)>
            queueMem{};
    eng::EventQueue<DamageEvent> queue(queueMem, 8);

    TEST_CHECK(queue.isEmpty());
    TEST_CHECK(queue.size() == 0);

    // Push 5 items
    for (int i = 1; i <= 5; ++i) {
        TEST_CHECK(queue.push(DamageEvent{.damage = i * 10}));
        TEST_CHECK(queue.size() == static_cast<size_t>(i));
    }

    // Drain into vector and verify strict FIFO ordering
    std::vector<int> received;
    const size_t drained =
        queue.drain([&received](const DamageEvent& e) { received.push_back(e.damage); });

    TEST_CHECK(drained == 5);
    TEST_CHECK(queue.isEmpty());
    TEST_CHECK(queue.size() == 0);
    TEST_CHECK(received.size() == 5);
    TEST_CHECK(received[0] == 10);
    TEST_CHECK(received[1] == 20);
    TEST_CHECK(received[2] == 30);
    TEST_CHECK(received[3] == 40);
    TEST_CHECK(received[4] == 50);

    return 0;
}

int testQueueCapacityAndOverflow() {
    constexpr size_t kCap = 3;
    alignas(eng::EventQueue<DamageEvent>::requiredAlignment())
        std::array<std::byte, eng::EventQueue<DamageEvent>::requiredBytes(kCap)>
            queueMem{};
    eng::EventQueue<DamageEvent> queue(queueMem, kCap);

    TEST_CHECK(queue.capacity() == kCap);

    // Fill to capacity
    TEST_CHECK(queue.push(DamageEvent{.damage = 1}));
    TEST_CHECK(queue.push(DamageEvent{.damage = 2}));
    TEST_CHECK(queue.push(DamageEvent{.damage = 3}));
    TEST_CHECK(queue.isFull());
    TEST_CHECK(queue.droppedCount() == 0);

    // Overflow attempt: returns false and increments droppedCount
    TEST_CHECK(!queue.push(DamageEvent{.damage = 99}));
    TEST_CHECK(queue.droppedCount() == 1);
    TEST_CHECK(queue.size() == kCap);

    TEST_CHECK(!queue.push(DamageEvent{.damage = 100}));
    TEST_CHECK(queue.droppedCount() == 2);

    // Drain one event
    int firstVal = 0;
    size_t drained = queue.drain([&firstVal](const DamageEvent& e) {
        if (firstVal == 0) {
            firstVal = e.damage;
        }
    });
    // drain() drains all current events (3)
    TEST_CHECK(drained == 3);
    TEST_CHECK(firstVal == 1);
    TEST_CHECK(queue.isEmpty());

    // Can enqueue again
    TEST_CHECK(queue.push(DamageEvent{.damage = 42}));
    TEST_CHECK(queue.size() == 1);

    return 0;
}

int testNonTrivialPayloadLifecycle() {
    TrackedObject::reset();

    constexpr size_t kCap = 4;
    alignas(eng::EventQueue<NonTrivialEvent>::requiredAlignment())
        std::array<std::byte, eng::EventQueue<NonTrivialEvent>::requiredBytes(kCap)>
            queueMem{};

    {
        eng::EventQueue<NonTrivialEvent> queue(queueMem, kCap);
        TEST_CHECK(TrackedObject::aliveCount() == 0);

        queue.emplace(10);
        queue.emplace(20);
        queue.emplace(30);

        TEST_CHECK(queue.size() == 3);
        TEST_CHECK(TrackedObject::sConstructed == 3);
        TEST_CHECK(TrackedObject::sDestructed == 0);
        TEST_CHECK(TrackedObject::aliveCount() == 3);

        // Drain 3 events
        int sum = 0;
        queue.drain([&sum](const NonTrivialEvent& e) { sum += e.payload.value; });

        TEST_CHECK(sum == 60);
        TEST_CHECK(queue.isEmpty());
        // All drained events must be explicitly destructed
        TEST_CHECK(TrackedObject::sDestructed == 3);
        TEST_CHECK(TrackedObject::aliveCount() == 0);

        // Enqueue remaining elements that will be destroyed when queue goes out of scope
        queue.emplace(100);
        queue.emplace(200);
        TEST_CHECK(TrackedObject::aliveCount() == 2);
    }

    // Queue destructor called clear(), destructing remaining 2 objects
    TEST_CHECK(TrackedObject::aliveCount() == 0);
    TEST_CHECK(TrackedObject::sConstructed == 5);
    TEST_CHECK(TrackedObject::sDestructed == 5);

    return 0;
}

int testEnqueueDuringDrain() {
    constexpr size_t kCap = 8;
    alignas(eng::EventQueue<DamageEvent>::requiredAlignment())
        std::array<std::byte, eng::EventQueue<DamageEvent>::requiredBytes(kCap)>
            queueMem{};
    eng::EventQueue<DamageEvent> queue(queueMem, kCap);

    queue.push(DamageEvent{.damage = 1});
    queue.push(DamageEvent{.damage = 2});

    TEST_CHECK(queue.size() == 2);

    std::vector<int> firstDrainResults;
    // Inside handler, enqueue an additional event
    const size_t drained = queue.drain([&](const DamageEvent& e) {
        firstDrainResults.push_back(e.damage);
        if (e.damage == 1) {
            queue.push(DamageEvent{.damage = 99});
        }
    });

    // Exactly 2 events drained in this pass (snapshot count)
    TEST_CHECK(drained == 2);
    TEST_CHECK(firstDrainResults.size() == 2);
    TEST_CHECK(firstDrainResults[0] == 1);
    TEST_CHECK(firstDrainResults[1] == 2);

    // Newly pushed event (99) remains in queue for next drain
    TEST_CHECK(queue.size() == 1);

    std::vector<int> secondDrainResults;
    const size_t secondDrained =
        queue.drain([&](const DamageEvent& e) { secondDrainResults.push_back(e.damage); });

    TEST_CHECK(secondDrained == 1);
    TEST_CHECK(secondDrainResults.size() == 1);
    TEST_CHECK(secondDrainResults[0] == 99);
    TEST_CHECK(queue.isEmpty());

    return 0;
}

int testMoveSemantics() {
    alignas(eng::EventDispatcher::requiredAlignment())
        std::array<std::byte, eng::EventDispatcher::requiredBytes(4)>
            dispMem{};
    eng::EventDispatcher disp1(dispMem, 4);

    int callCount = 0;
    auto listener = [](const DamageEvent&, void* ctx) { ++(*static_cast<int*>(ctx)); };
    const auto t = disp1.registerListener<DamageEvent>(listener, &callCount);
    TEST_CHECK(t.isValid());

    // Move construct dispatcher
    eng::EventDispatcher disp2(std::move(disp1));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    TEST_CHECK(disp1.isEmpty());
    TEST_CHECK(disp2.size() == 1);

    disp2.dispatch(DamageEvent{.damage = 1});
    TEST_CHECK(callCount == 1);

    // Self-move dispatcher
    auto* selfPtr = &disp2;
    disp2 = std::move(*selfPtr);
    TEST_CHECK(disp2.size() == 1);

    // Move construct queue
    alignas(eng::EventQueue<DamageEvent>::requiredAlignment())
        std::array<std::byte, eng::EventQueue<DamageEvent>::requiredBytes(4)>
            qMem{};
    eng::EventQueue<DamageEvent> q1(qMem, 4);
    q1.push(DamageEvent{.damage = 10});
    q1.push(DamageEvent{.damage = 20});

    eng::EventQueue<DamageEvent> q2(std::move(q1));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    TEST_CHECK(q1.isEmpty());
    TEST_CHECK(q2.size() == 2);

    // Self-move queue
    auto* qSelf = &q2;
    q2 = std::move(*qSelf);
    TEST_CHECK(q2.size() == 2);

    int drainedSum = 0;
    q2.drain([&drainedSum](const DamageEvent& e) { drainedSum += e.damage; });
    TEST_CHECK(drainedSum == 30);

    return 0;
}

} // namespace

int main() {
    if (testTypedDispatch() != 0) {
        return 1;
    }
    if (testMultipleListeners() != 0) {
        return 2;
    }
    if (testListenerRegistrationAndRemoval() != 0) {
        return 3;
    }
    if (testInvalidAndStaleTokens() != 0) {
        return 4;
    }
    if (testDispatchWithoutListeners() != 0) {
        return 5;
    }
    if (testMemberFunctionListener() != 0) {
        return 6;
    }
    if (testListenerRemovalDuringDispatch() != 0) {
        return 7;
    }
    if (testListenerRegistrationDuringDispatch() != 0) {
        return 8;
    }
    if (testQueueFifoOrdering() != 0) {
        return 9;
    }
    if (testQueueCapacityAndOverflow() != 0) {
        return 10;
    }
    if (testNonTrivialPayloadLifecycle() != 0) {
        return 11;
    }
    if (testEnqueueDuringDrain() != 0) {
        return 12;
    }
    if (testMoveSemantics() != 0) {
        return 13;
    }

    return 0;
}
