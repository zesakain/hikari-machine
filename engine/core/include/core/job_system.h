/// @file job_system.h
/// @brief Core bounded Job System with dependency tracking, worker management, and O(1) scheduling.
///
/// Purpose:
/// Provides parallel job scheduling and execution without dynamic heap allocation on scheduling
/// paths. Supports fine-grained dependency directed acyclic graphs (DAGs), safe cycle rejection,
/// configurable worker threads, work-helping in wait() to eliminate deadlocks, and generational
/// stale-handle detection.
///
/// Ownership and Lifetime Contract:
/// - Backing Storage: The JobSystem does NOT own its backing buffer. Callers provide a
/// pre-allocated
///   `std::span<std::byte>` sized to `requiredBytes(config)` and aligned to `requiredAlignment()`.
///   The backing memory must strictly outlive the JobSystem instance.
/// - Worker Threads: Worker threads are owned directly by the JobSystem and terminated during
///   shutdown() or destruction.
/// - Callables & Context: Callables are static function pointers (`JobFunction` or
/// `VoidJobFunction`).
///   Context pointers are non-owning; the caller is responsible for maintaining the lifetime of any
///   data pointed to by `context` until the job completes.
///
/// Thread Safety:
/// - submit(), wait(), waitAll(), addDependency(), isComplete(), state(), reclaim(), and shutdown()
///   are thread-safe and can be called concurrently from the main thread, worker threads, or helper
///   jobs.
/// - Job callbacks are executed WITHOUT holding internal scheduler locks, allowing jobs to submit
///   child jobs or wait on subtasks safely.
///
/// Deadlock Prevention:
/// - wait() and waitAll() actively help execute ready jobs while waiting, preventing thread-pool
///   starvation and deadlocks when worker threads wait on dependent sub-jobs.
///
/// Performance:
/// - Zero heap allocation during submit(), wait(), or execution.
/// - Bounded job slot and dependency edge pools.
/// - O(1) job dispatch and ready-queue operations.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <core/align.h>
#include <core/assert.h>
#include <core/error.h>
#include <core/generational_handle.h>
#include <core/result.h>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>

namespace eng {

/// @struct JobHandle
/// @brief 64-bit generational handle uniquely identifying an active or completed job.
struct JobHandle {
    GenerationalHandle handle;

    constexpr JobHandle() noexcept = default;
    constexpr explicit JobHandle(GenerationalHandle h) noexcept : handle(h) {}
    constexpr JobHandle(uint32_t index, uint32_t generation) noexcept : handle(index, generation) {}

    [[nodiscard]] constexpr bool isValid() const noexcept { return handle.isValid(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }
    [[nodiscard]] constexpr uint32_t index() const noexcept { return handle.index; }
    [[nodiscard]] constexpr uint32_t generation() const noexcept { return handle.generation; }

    [[nodiscard]] static constexpr JobHandle invalid() noexcept {
        return JobHandle{GenerationalHandle::invalid()};
    }

    friend constexpr bool operator==(const JobHandle&, const JobHandle&) noexcept = default;
};

/// @enum JobState
/// @brief Lifecycle state of a job within the JobSystem.
enum class JobState : uint8_t {
    Unallocated = 0, ///< Slot is unassigned or retired.
    Pending,         ///< Waiting for dependencies to complete.
    Ready,           ///< In the ready queue, eligible for execution.
    Running,         ///< Currently executing on a worker or helper thread.
    Succeeded,       ///< Completed successfully (returned true or void).
    Failed,          ///< Execution failed (returned false or dependency failed).
    Canceled         ///< Canceled prior to execution (e.g. during shutdown).
};

/// @brief Converts JobState to a stable uppercase string literal.
[[nodiscard]] constexpr std::string_view toString(JobState state) noexcept {
    switch (state) {
    case JobState::Unallocated:
        return "UNALLOCATED";
    case JobState::Pending:
        return "PENDING";
    case JobState::Ready:
        return "READY";
    case JobState::Running:
        return "RUNNING";
    case JobState::Succeeded:
        return "SUCCEEDED";
    case JobState::Failed:
        return "FAILED";
    case JobState::Canceled:
        return "CANCELED";
    }
    return "UNKNOWN";
}

/// @brief Signature for job functions returning success (true) or failure (false).
using JobFunction = bool (*)(void* context) noexcept;

/// @brief Signature for job functions that do not return status (always succeed).
using VoidJobFunction = void (*)(void* context) noexcept;

/// @struct JobDesc
/// @brief Declaration parameters for submitting a job.
struct JobDesc {
    JobFunction function{nullptr};
    VoidJobFunction voidFunction{nullptr};
    void* context{nullptr};
    std::span<const JobHandle> dependencies;

    JobDesc() noexcept = default;

    JobDesc(JobFunction fn, void* ctx, std::span<const JobHandle> deps = {}) noexcept
        : function(fn), context(ctx), dependencies(deps) {}

    JobDesc(VoidJobFunction fn, void* ctx, std::span<const JobHandle> deps = {}) noexcept
        : voidFunction(fn), context(ctx), dependencies(deps) {}
};

/// @struct JobSystemConfig
/// @brief Capacity and worker configuration for JobSystem initialization.
struct JobSystemConfig {
    static constexpr uint32_t kDefaultMaxJobs = 256;
    static constexpr uint32_t kDefaultMaxDependencies = 1024;
    static constexpr uint32_t kMaxSupportedWorkers = 64;

    uint32_t workerCount{0};                           ///< Number of background worker threads.
    uint32_t maxJobs{kDefaultMaxJobs};                 ///< Maximum concurrent jobs tracked.
    uint32_t maxDependencies{kDefaultMaxDependencies}; ///< Maximum concurrent dependency edges.
};

/// @class JobSystem
/// @brief Bounded, thread-safe job scheduler with dependency tracking and zero hot-path allocation.
class JobSystem {
public:
    static constexpr uint32_t kMaxSupportedWorkers = JobSystemConfig::kMaxSupportedWorkers;
    static constexpr uint32_t kMaxInlinePredecessors = 8;
    static constexpr uint32_t kInvalidIndex = 0xFFFF'FFFF;

    /// @struct JobSlot
    /// @brief Storage layout for a single job slot.
    struct JobSlot {
        uint32_t generation{1};
        JobFunction function{nullptr};
        VoidJobFunction voidFunction{nullptr};
        void* context{nullptr};
        std::atomic<JobState> state{JobState::Unallocated};
        std::atomic<uint32_t> unresolvedDependencies{0};
        uint32_t firstSuccessorEdge{kInvalidIndex};
        uint32_t predecessorCount{0};
        std::array<uint32_t, kMaxInlinePredecessors> predecessors{};
    };

    /// @struct DependencyEdge
    /// @brief Storage layout for an edge in the dependency DAG.
    struct DependencyEdge {
        uint32_t successorJobIndex{kInvalidIndex};
        uint32_t nextEdgeIndex{kInvalidIndex};
    };

    /// @brief Returns minimum memory alignment required for backing storage.
    [[nodiscard]] static constexpr size_t requiredAlignment() noexcept {
        return std::max(
            {alignof(JobSlot), alignof(DependencyEdge), alignof(uint32_t), alignof(void*)});
    }

    /// @brief Calculates required backing bytes for the given configuration.
    [[nodiscard]] static size_t requiredBytes(const JobSystemConfig& config) noexcept;

    /// @brief Constructs JobSystem over caller-provided backing memory.
    JobSystem(std::span<std::byte> storage, const JobSystemConfig& config);

    /// @brief Destructor. Shuts down workers and cleans up internal slots.
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    /// @brief Submits a job with detailed descriptor.
    [[nodiscard]] Result<JobHandle, Error> submit(const JobDesc& desc) noexcept;

    /// @brief Submits a job returning boolean success/failure.
    [[nodiscard]] Result<JobHandle, Error> submit(JobFunction fn, void* ctx = nullptr,
                                                  std::span<const JobHandle> deps = {}) noexcept;

    /// @brief Submits a job returning void (always succeeds).
    [[nodiscard]] Result<JobHandle, Error> submit(VoidJobFunction fn, void* ctx = nullptr,
                                                  std::span<const JobHandle> deps = {}) noexcept;

    /// @brief Adds a dependency between two existing active jobs.
    /// Rejects dependency cycles explicitly with ErrorCode::InvalidArgument.
    [[nodiscard]] Result<void, Error> addDependency(JobHandle target,
                                                    JobHandle dependency) noexcept;

    /// @brief Waits for a job to complete, helping execute pending ready work in the meantime.
    [[nodiscard]] Result<JobState, Error> wait(JobHandle handle) noexcept;

    /// @brief Waits for all active jobs to complete, helping execute ready work in the meantime.
    [[nodiscard]] Result<void, Error> waitAll() noexcept;

    /// @brief Checks if a job has completed execution (Succeeded, Failed, or Canceled).
    [[nodiscard]] bool isComplete(JobHandle handle) const noexcept;

    /// @brief Checks if a job completed successfully.
    [[nodiscard]] bool isSucceeded(JobHandle handle) const noexcept;

    /// @brief Checks if a job failed execution or was aborted due to failed dependencies.
    [[nodiscard]] bool isFailed(JobHandle handle) const noexcept;

    /// @brief Returns the current state of a job.
    [[nodiscard]] JobState state(JobHandle handle) const noexcept;

    /// @brief Validates whether a handle points to an active or retained job with matching
    /// generation.
    [[nodiscard]] bool isValidHandle(JobHandle handle) const noexcept;

    /// @brief Explicitly reclaims a completed job slot, advancing its generation.
    [[nodiscard]] Result<void, Error> reclaim(JobHandle handle) noexcept;

    /// @brief Shuts down workers, cancels pending work, and halts execution.
    void shutdown() noexcept;

    /// @brief Reinitializes worker threads after shutdown.
    [[nodiscard]] Result<void, Error> initializeWorkers(uint32_t workerCount) noexcept;

    [[nodiscard]] uint32_t workerCount() const noexcept { return workerCount_; }
    [[nodiscard]] uint32_t maxJobs() const noexcept { return maxJobs_; }
    [[nodiscard]] uint32_t maxDependencies() const noexcept { return maxDependencies_; }
    [[nodiscard]] uint32_t activeJobCount() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept {
        return !stopping_.load(std::memory_order_acquire);
    }

private:
    void workerLoop(uint32_t workerIndex) noexcept;
    void executeJob(uint32_t jobIndex) noexcept;
    void finishJob(uint32_t jobIndex, bool success) noexcept;
    void pushReadyQueueLocked(uint32_t jobIndex) noexcept;
    void reclaimCompletedSlotsLocked() noexcept;
    [[nodiscard]] bool transitivelyDependsOnLocked(uint32_t fromJobIndex,
                                                   uint32_t targetJobIndex) const noexcept;
    [[nodiscard]] bool isValidHandleLocked(JobHandle handle) const noexcept;
    [[nodiscard]] uint32_t activeJobCountLocked() const noexcept;

    uint32_t maxJobs_{0};
    uint32_t maxDependencies_{0};

    JobSlot* jobs_{nullptr};
    DependencyEdge* edges_{nullptr};
    uint32_t* readyQueue_{nullptr};
    uint32_t* slotFreeList_{nullptr};
    uint32_t* edgeFreeList_{nullptr};

    uint32_t freeSlotCount_{0};
    uint32_t freeEdgeCount_{0};
    uint32_t readyHead_{0};
    uint32_t readyTail_{0};
    uint32_t readyCount_{0};

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable completionCv_;

    std::atomic<bool> stopping_{false};
    std::array<std::thread, kMaxSupportedWorkers> workers_{};
    uint32_t workerCount_{0};
};

} // namespace eng
