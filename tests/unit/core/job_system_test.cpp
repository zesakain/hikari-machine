/// @file job_system_test.cpp
/// @brief Comprehensive unit and thread-safety tests for eng::JobSystem.

#include <array>
#include <atomic>
#include <chrono>
#include <core/job_system.h>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

#define TEST_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

struct BasicContext {
    int value{0};
    bool executed{false};
};

int testSubmissionAndExecution() {
    eng::JobSystemConfig config{.workerCount = 2, .maxJobs = 16, .maxDependencies = 32};
    alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 16384> storage{};
    TEST_CHECK(storage.size() >= eng::JobSystem::requiredBytes(config));

    eng::JobSystem js(storage, config);
    TEST_CHECK(js.workerCount() == 2);
    TEST_CHECK(js.isRunning());

    BasicContext ctx1{.value = 42, .executed = false};
    auto voidJob = [](void* c) noexcept {
        auto* ctx = static_cast<BasicContext*>(c);
        ctx->value += 10;
        ctx->executed = true;
    };

    auto h1Res = js.submit(voidJob, &ctx1);
    TEST_CHECK(h1Res.hasValue());
    auto h1 = h1Res.value();
    TEST_CHECK(h1.isValid());

    auto wait1Res = js.wait(h1);
    TEST_CHECK(wait1Res.hasValue());
    TEST_CHECK(wait1Res.value() == eng::JobState::Succeeded);
    TEST_CHECK(ctx1.executed);
    TEST_CHECK(ctx1.value == 52);
    TEST_CHECK(js.isComplete(h1));
    TEST_CHECK(js.isSucceeded(h1));
    TEST_CHECK(!js.isFailed(h1));

    BasicContext ctx2{.value = 100, .executed = false};
    auto boolJob = [](void* c) noexcept -> bool {
        auto* ctx = static_cast<BasicContext*>(c);
        ctx->value *= 2;
        ctx->executed = true;
        return true;
    };

    auto h2Res = js.submit(boolJob, &ctx2);
    TEST_CHECK(h2Res.hasValue());
    auto h2 = h2Res.value();

    auto wait2Res = js.wait(h2);
    TEST_CHECK(wait2Res.hasValue());
    TEST_CHECK(wait2Res.value() == eng::JobState::Succeeded);
    TEST_CHECK(ctx2.executed);
    TEST_CHECK(ctx2.value == 200);

    return 0;
}

int testMultipleWorkers() {
    eng::JobSystemConfig config{.workerCount = 4, .maxJobs = 32, .maxDependencies = 64};
    alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 32768> storage{};
    eng::JobSystem js(storage, config);

    struct WorkerRecord {
        std::thread::id threadId;
        std::atomic<bool> done{false};
    };

    constexpr size_t kJobCount = 8;
    std::array<WorkerRecord, kJobCount> records{};

    auto jobFn = [](void* c) noexcept {
        auto* r = static_cast<WorkerRecord*>(c);
        r->threadId = std::this_thread::get_id();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        r->done.store(true, std::memory_order_release);
    };

    std::array<eng::JobHandle, kJobCount> handles{};
    for (size_t i = 0; i < kJobCount; ++i) {
        auto res = js.submit(jobFn, &records[i]);
        TEST_CHECK(res.hasValue());
        handles[i] = res.value();
    }

    auto waitRes = js.waitAll();
    TEST_CHECK(waitRes.hasValue());

    std::vector<std::thread::id> distinctThreads;
    for (size_t i = 0; i < kJobCount; ++i) {
        TEST_CHECK(records[i].done.load(std::memory_order_acquire));
        bool found = false;
        for (const auto& tid : distinctThreads) {
            if (tid == records[i].threadId) {
                found = true;
                break;
            }
        }
        if (!found) {
            distinctThreads.push_back(records[i].threadId);
        }
    }

    // At least 2 distinct threads should have participated with 4 workers and concurrent sleep
    TEST_CHECK(distinctThreads.size() >= 2);

    return 0;
}

int testDependencyOrdering() {
    eng::JobSystemConfig config{.workerCount = 2, .maxJobs = 16, .maxDependencies = 32};
    alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 16384> storage{};
    eng::JobSystem js(storage, config);

    struct OrderContext {
        std::atomic<int> step{0};
        bool orderCorrect{true};
    } ctx;

    auto job1 = [](void* c) noexcept {
        auto* ctxPtr = static_cast<OrderContext*>(c);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ctxPtr->step.store(1, std::memory_order_release);
    };

    auto job2 = [](void* c) noexcept {
        auto* ctxPtr = static_cast<OrderContext*>(c);
        if (ctxPtr->step.load(std::memory_order_acquire) != 1) {
            ctxPtr->orderCorrect = false;
        }
        ctxPtr->step.store(2, std::memory_order_release);
    };

    auto job3 = [](void* c) noexcept {
        auto* ctxPtr = static_cast<OrderContext*>(c);
        if (ctxPtr->step.load(std::memory_order_acquire) != 2) {
            ctxPtr->orderCorrect = false;
        }
        ctxPtr->step.store(3, std::memory_order_release);
    };

    auto h1Res = js.submit(job1, &ctx);
    TEST_CHECK(h1Res.hasValue());

    std::array<eng::JobHandle, 1> dep1{h1Res.value()};
    auto h2Res = js.submit(job2, &ctx, dep1);
    TEST_CHECK(h2Res.hasValue());

    std::array<eng::JobHandle, 1> dep2{h2Res.value()};
    auto h3Res = js.submit(job3, &ctx, dep2);
    TEST_CHECK(h3Res.hasValue());

    auto waitRes = js.wait(h3Res.value());
    TEST_CHECK(waitRes.hasValue());
    TEST_CHECK(waitRes.value() == eng::JobState::Succeeded);
    TEST_CHECK(ctx.orderCorrect);
    TEST_CHECK(ctx.step.load(std::memory_order_acquire) == 3);

    return 0;
}

int testMultipleDependencies() {
    eng::JobSystemConfig config{.workerCount = 4, .maxJobs = 32, .maxDependencies = 64};
    alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 32768> storage{};
    eng::JobSystem js(storage, config);

    struct MultiContext {
        int valA{0};
        int valB{0};
        int valC{0};
        int sum{0};
        bool allPresent{false};
    } ctx;

    auto jobA = [](void* c) noexcept { static_cast<MultiContext*>(c)->valA = 10; };
    auto jobB = [](void* c) noexcept { static_cast<MultiContext*>(c)->valB = 20; };
    auto jobC = [](void* c) noexcept { static_cast<MultiContext*>(c)->valC = 30; };

    auto hA = js.submit(jobA, &ctx).value();
    auto hB = js.submit(jobB, &ctx).value();
    auto hC = js.submit(jobC, &ctx).value();

    std::array<eng::JobHandle, 3> deps{hA, hB, hC};
    auto jobD = [](void* c) noexcept {
        auto* ctxPtr = static_cast<MultiContext*>(c);
        if (ctxPtr->valA == 10 && ctxPtr->valB == 20 && ctxPtr->valC == 30) {
            ctxPtr->allPresent = true;
        }
        ctxPtr->sum = ctxPtr->valA + ctxPtr->valB + ctxPtr->valC;
    };

    auto hD = js.submit(jobD, &ctx, deps).value();
    auto waitRes = js.wait(hD);
    TEST_CHECK(waitRes.hasValue());
    TEST_CHECK(waitRes.value() == eng::JobState::Succeeded);
    TEST_CHECK(ctx.allPresent);
    TEST_CHECK(ctx.sum == 60);

    return 0;
}

int testCompletionAndFailureState() {
    eng::JobSystemConfig config{.workerCount = 2, .maxJobs = 16, .maxDependencies = 32};
    alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 16384> storage{};
    eng::JobSystem js(storage, config);

    auto failJob = [](void*) noexcept -> bool { return false; };

    auto hFailRes = js.submit(failJob, nullptr);
    TEST_CHECK(hFailRes.hasValue());
    auto hFail = hFailRes.value();

    auto waitFail = js.wait(hFail);
    TEST_CHECK(waitFail.hasValue());
    TEST_CHECK(waitFail.value() == eng::JobState::Failed);
    TEST_CHECK(js.isComplete(hFail));
    TEST_CHECK(js.isFailed(hFail));
    TEST_CHECK(!js.isSucceeded(hFail));

    // Dependent job should fail without executing
    bool dependentRan = false;
    auto depJob = [](void* c) noexcept { *static_cast<bool*>(c) = true; };

    std::array<eng::JobHandle, 1> failDeps{hFail};
    auto hDepRes = js.submit(depJob, &dependentRan, failDeps);
    TEST_CHECK(hDepRes.hasValue());
    auto hDep = hDepRes.value();

    auto waitDep = js.wait(hDep);
    TEST_CHECK(waitDep.hasValue());
    TEST_CHECK(waitDep.value() == eng::JobState::Failed);
    TEST_CHECK(!dependentRan);
    TEST_CHECK(js.isFailed(hDep));

    return 0;
}

int testCapacityAndOverflow() {
    eng::JobSystemConfig config{.workerCount = 0, .maxJobs = 4, .maxDependencies = 8};
    alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 8192> storage{};
    eng::JobSystem js(storage, config);

    auto noopJob = [](void*) noexcept {};

    auto h1 = js.submit(noopJob, nullptr);
    auto h2 = js.submit(noopJob, nullptr);
    auto h3 = js.submit(noopJob, nullptr);
    auto h4 = js.submit(noopJob, nullptr);

    TEST_CHECK(h1.hasValue());
    TEST_CHECK(h2.hasValue());
    TEST_CHECK(h3.hasValue());
    TEST_CHECK(h4.hasValue());

    // 5th submit should fail due to full capacity
    auto h5 = js.submit(noopJob, nullptr);
    TEST_CHECK(!h5.hasValue());
    TEST_CHECK(h5.error().code == eng::ErrorCode::OutOfMemory);

    // Wait and complete h1
    auto waitRes = js.wait(h1.value());
    TEST_CHECK(waitRes.hasValue());

    // Reclaim h1
    auto reclaimRes = js.reclaim(h1.value());
    TEST_CHECK(reclaimRes.hasValue());

    // Now submitting h5 should succeed
    auto h5Retry = js.submit(noopJob, nullptr);
    TEST_CHECK(h5Retry.hasValue());

    return 0;
}

int testInvalidAndStaleHandles() {
    eng::JobSystemConfig config{.workerCount = 1, .maxJobs = 4, .maxDependencies = 8};
    alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 8192> storage{};
    eng::JobSystem js(storage, config);

    // Sentinel invalid handle
    eng::JobHandle inv = eng::JobHandle::invalid();
    TEST_CHECK(!js.isValidHandle(inv));
    TEST_CHECK(!js.isComplete(inv));
    TEST_CHECK(!js.isSucceeded(inv));
    TEST_CHECK(!js.isFailed(inv));
    TEST_CHECK(js.state(inv) == eng::JobState::Unallocated);
    TEST_CHECK(!js.wait(inv).hasValue());

    // Out of bounds handle
    eng::JobHandle oob(99, 1);
    TEST_CHECK(!js.isValidHandle(oob));
    TEST_CHECK(!js.wait(oob).hasValue());

    // Unallocated slot with generation 1 (fabricated handle for slot 3 that was never submitted)
    eng::JobHandle unallocated(3, 1);
    TEST_CHECK(!js.isValidHandle(unallocated));
    TEST_CHECK(!js.isComplete(unallocated));
    TEST_CHECK(!js.isSucceeded(unallocated));
    TEST_CHECK(!js.isFailed(unallocated));
    TEST_CHECK(js.state(unallocated) == eng::JobState::Unallocated);
    TEST_CHECK(!js.wait(unallocated).hasValue());

    // Submit and reclaim to create a stale handle
    auto h = js.submit([](void*) noexcept {}, nullptr).value();
    TEST_CHECK(js.wait(h).hasValue());
    TEST_CHECK(js.reclaim(h).hasValue());

    // h is now stale
    TEST_CHECK(!js.isValidHandle(h));
    TEST_CHECK(!js.isComplete(h));
    TEST_CHECK(!js.isSucceeded(h));
    TEST_CHECK(!js.isFailed(h));
    TEST_CHECK(js.state(h) == eng::JobState::Unallocated);

    auto waitStale = js.wait(h);
    TEST_CHECK(!waitStale.hasValue());
    TEST_CHECK(waitStale.error().code == eng::ErrorCode::HandleResolutionFailed);

    return 0;
}

int testShutdownWithPendingJobs() {
    // Part 1: Verify pending and ready jobs are transitioned to Canceled on shutdown
    {
        eng::JobSystemConfig config{.workerCount = 0, .maxJobs = 16, .maxDependencies = 32};
        alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 16384> storage{};
        eng::JobSystem js(storage, config);

        auto hReady = js.submit([](void*) noexcept {}, nullptr).value();
        std::array<eng::JobHandle, 1> deps{hReady};
        auto hPending = js.submit([](void*) noexcept {}, nullptr, deps).value();

        TEST_CHECK(js.state(hReady) == eng::JobState::Ready);
        TEST_CHECK(js.state(hPending) == eng::JobState::Pending);

        js.shutdown();

        TEST_CHECK(!js.isRunning());
        TEST_CHECK(js.state(hReady) == eng::JobState::Canceled);
        TEST_CHECK(js.state(hPending) == eng::JobState::Canceled);

        // Submitting after shutdown must fail
        auto hAfter = js.submit([](void*) noexcept {}, nullptr);
        TEST_CHECK(!hAfter.hasValue());
        TEST_CHECK(hAfter.error().code == eng::ErrorCode::InvalidState);
    }

    // Part 2: Verify workers cleanly stop and join during shutdown
    {
        eng::JobSystemConfig config{.workerCount = 2, .maxJobs = 16, .maxDependencies = 32};
        alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 16384> storage{};
        eng::JobSystem js(storage, config);

        std::atomic<bool> jobDone{false};
        auto h = js.submit(
                       [](void* c) noexcept {
                           auto* d = static_cast<std::atomic<bool>*>(c);
                           d->store(true, std::memory_order_release);
                       },
                       &jobDone)
                     .value();

        TEST_CHECK(js.wait(h).hasValue());
        TEST_CHECK(jobDone.load(std::memory_order_acquire));

        js.shutdown();
        TEST_CHECK(!js.isRunning());
        TEST_CHECK(js.workerCount() == 0);
    }

    return 0;
}

int testWaitAndDeadlockPrevention() {
    // 0 workers: pure work-helping on caller thread
    eng::JobSystemConfig config{.workerCount = 0, .maxJobs = 16, .maxDependencies = 32};
    alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 16384> storage{};
    eng::JobSystem js(storage, config);

    int counter = 0;
    auto h1 = js.submit([](void* c) noexcept { *static_cast<int*>(c) += 1; }, &counter).value();
    std::array<eng::JobHandle, 1> dep1{h1};
    auto h2 =
        js.submit([](void* c) noexcept { *static_cast<int*>(c) += 2; }, &counter, dep1).value();
    std::array<eng::JobHandle, 1> dep2{h2};
    auto h3 =
        js.submit([](void* c) noexcept { *static_cast<int*>(c) += 3; }, &counter, dep2).value();

    // Calling wait(h3) executes h1, h2, h3 in sequence on caller thread
    auto waitRes = js.wait(h3);
    TEST_CHECK(waitRes.hasValue());
    TEST_CHECK(waitRes.value() == eng::JobState::Succeeded);
    TEST_CHECK(counter == 6);

    // Nested job test: Job A submits Job B and waits for it
    struct NestedContext {
        eng::JobSystem* jobSys{nullptr};
        int innerValue{0};
        bool success{false};
    } nestedCtx{.jobSys = &js};

    auto outerJob = [](void* c) noexcept {
        auto* ctx = static_cast<NestedContext*>(c);
        auto innerRes = ctx->jobSys->submit(
            [](void* innerCtx) noexcept {
                static_cast<NestedContext*>(innerCtx)->innerValue = 777;
            },
            ctx);
        if (innerRes.hasValue()) {
            auto w = ctx->jobSys->wait(innerRes.value());
            if (w.hasValue() && w.value() == eng::JobState::Succeeded) {
                ctx->success = true;
            }
        }
    };

    auto hOuter = js.submit(outerJob, &nestedCtx).value();
    auto wOuter = js.wait(hOuter);
    TEST_CHECK(wOuter.hasValue());
    TEST_CHECK(nestedCtx.success);
    TEST_CHECK(nestedCtx.innerValue == 777);

    return 0;
}

int testWorkerLifecycleAndRepeatedInit() {
    eng::JobSystemConfig config{.workerCount = 2, .maxJobs = 16, .maxDependencies = 32};
    alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 16384> storage{};
    eng::JobSystem js(storage, config);

    int val = 0;
    auto h1 = js.submit([](void* c) noexcept { *static_cast<int*>(c) = 1; }, &val).value();
    TEST_CHECK(js.wait(h1).hasValue());
    TEST_CHECK(val == 1);

    // First shutdown
    js.shutdown();
    TEST_CHECK(js.workerCount() == 0);
    TEST_CHECK(!js.isRunning());

    // Re-initialize with 4 workers
    auto initRes = js.initializeWorkers(4);
    TEST_CHECK(initRes.hasValue());
    TEST_CHECK(js.workerCount() == 4);
    TEST_CHECK(js.isRunning());

    auto h2 = js.submit([](void* c) noexcept { *static_cast<int*>(c) = 2; }, &val).value();
    TEST_CHECK(js.wait(h2).hasValue());
    TEST_CHECK(val == 2);

    // Second shutdown
    js.shutdown();
    TEST_CHECK(js.workerCount() == 0);

    return 0;
}

int testCycleDetection() {
    eng::JobSystemConfig config{.workerCount = 0, .maxJobs = 16, .maxDependencies = 32};
    alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 16384> storage{};
    eng::JobSystem js(storage, config);

    auto hA = js.submit([](void*) noexcept {}, nullptr).value();
    auto hB = js.submit([](void*) noexcept {}, nullptr).value();
    auto hC = js.submit([](void*) noexcept {}, nullptr).value();

    // Self-dependency rejected
    auto selfRes = js.addDependency(hA, hA);
    TEST_CHECK(!selfRes.hasValue());
    TEST_CHECK(selfRes.error().code == eng::ErrorCode::InvalidArgument);

    // A depends on B
    auto depRes1 = js.addDependency(hA, hB);
    TEST_CHECK(depRes1.hasValue());

    // Direct cycle: B cannot depend on A
    auto directCycle = js.addDependency(hB, hA);
    TEST_CHECK(!directCycle.hasValue());
    TEST_CHECK(directCycle.error().code == eng::ErrorCode::InvalidArgument);

    // Transitive chain: B depends on C
    auto depRes2 = js.addDependency(hB, hC);
    TEST_CHECK(depRes2.hasValue());

    // Transitive cycle: C cannot depend on A
    auto transCycle = js.addDependency(hC, hA);
    TEST_CHECK(!transCycle.hasValue());
    TEST_CHECK(transCycle.error().code == eng::ErrorCode::InvalidArgument);

    return 0;
}

int testStressJobs() {
    eng::JobSystemConfig config{.workerCount = 4, .maxJobs = 512, .maxDependencies = 1024};
    alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 131072> storage{};
    eng::JobSystem js(storage, config);

    constexpr int kTotalBatches = 5;
    constexpr int kJobsPerBatch = 64;
    std::atomic<int> counter{0};

    for (int b = 0; b < kTotalBatches; ++b) {
        std::array<eng::JobHandle, kJobsPerBatch> handles{};
        for (int i = 0; i < kJobsPerBatch; ++i) {
            auto res = js.submit(
                [](void* c) noexcept {
                    static_cast<std::atomic<int>*>(c)->fetch_add(1, std::memory_order_relaxed);
                },
                &counter);
            TEST_CHECK(res.hasValue());
            handles[static_cast<size_t>(i)] = res.value();
        }

        for (int i = 0; i < kJobsPerBatch; ++i) {
            auto w = js.wait(handles[static_cast<size_t>(i)]);
            TEST_CHECK(w.hasValue());
            TEST_CHECK(w.value() == eng::JobState::Succeeded);
        }
    }

    TEST_CHECK(counter.load(std::memory_order_acquire) == kTotalBatches * kJobsPerBatch);

    return 0;
}

int testJobSystemGenerationOverflowSafety() {
    eng::JobSystemConfig config{.workerCount = 0, .maxJobs = 2, .maxDependencies = 4};
    alignas(eng::JobSystem::requiredAlignment()) std::array<std::byte, 8192> storage{};
    eng::JobSystem js(storage, config);

    // Set slot 0 to near-overflow generation
    constexpr uint32_t kNearMax = eng::JobSystem::kRetiredGeneration - 1;
    js.setSlotGenerationForTesting(0, kNearMax);
    TEST_CHECK(js.slotGenerationForTesting(0) == kNearMax);

    auto h = js.submit([](void*) noexcept {}, nullptr).value();
    TEST_CHECK(h.index() == 0);
    TEST_CHECK(h.generation() == kNearMax);
    TEST_CHECK(js.wait(h).hasValue());

    // Reclaiming slot 0 advances it to kRetiredGeneration and retires it
    TEST_CHECK(js.reclaim(h).hasValue());
    TEST_CHECK(js.slotGenerationForTesting(0) == eng::JobSystem::kRetiredGeneration);

    // Fabricated retired handle is rejected
    eng::JobHandle retiredHandle(0, eng::JobSystem::kRetiredGeneration);
    TEST_CHECK(!js.isValidHandle(retiredHandle));
    TEST_CHECK(!js.isComplete(retiredHandle));
    TEST_CHECK(!js.isSucceeded(retiredHandle));
    TEST_CHECK(!js.isFailed(retiredHandle));
    TEST_CHECK(js.state(retiredHandle) == eng::JobState::Unallocated);

    // Next submit uses slot 1 (since slot 0 is retired)
    js.setSlotGenerationForTesting(1, kNearMax);
    auto hNext = js.submit([](void*) noexcept {}, nullptr).value();
    TEST_CHECK(hNext.index() == 1);
    TEST_CHECK(hNext.generation() == kNearMax);
    TEST_CHECK(js.wait(hNext).hasValue());

    // Reclaiming slot 1 also advances it to kRetiredGeneration and retires it
    TEST_CHECK(js.reclaim(hNext).hasValue());
    TEST_CHECK(js.slotGenerationForTesting(1) == eng::JobSystem::kRetiredGeneration);

    // Now, both slot 0 and slot 1 are permanently retired; next submit must fail with OutOfMemory
    auto hFail = js.submit([](void*) noexcept {}, nullptr);
    TEST_CHECK(!hFail.hasValue());
    TEST_CHECK(hFail.error().code == eng::ErrorCode::OutOfMemory);

    return 0;
}

} // namespace

int main() {
    if (testSubmissionAndExecution() != 0) {
        return 1;
    }
    if (testMultipleWorkers() != 0) {
        return 2;
    }
    if (testDependencyOrdering() != 0) {
        return 3;
    }
    if (testMultipleDependencies() != 0) {
        return 4;
    }
    if (testCompletionAndFailureState() != 0) {
        return 5;
    }
    if (testCapacityAndOverflow() != 0) {
        return 6;
    }
    if (testInvalidAndStaleHandles() != 0) {
        return 7;
    }
    if (testShutdownWithPendingJobs() != 0) {
        return 8;
    }
    if (testWaitAndDeadlockPrevention() != 0) {
        return 9;
    }
    if (testWorkerLifecycleAndRepeatedInit() != 0) {
        return 10;
    }
    if (testCycleDetection() != 0) {
        return 11;
    }
    if (testStressJobs() != 0) {
        return 12;
    }
    if (testJobSystemGenerationOverflowSafety() != 0) {
        return 13;
    }

    return 0;
}
