/// @file job_system.cpp
/// @brief Implementation of Core bounded Job System with dependency tracking.

#include <core/job_system.h>
#include <new>

namespace eng {

size_t JobSystem::requiredBytes(const JobSystemConfig& config) noexcept {
    size_t offset = 0;
    offset = alignUp(offset, alignof(JobSlot));
    offset += static_cast<size_t>(config.maxJobs) * sizeof(JobSlot);

    offset = alignUp(offset, alignof(DependencyEdge));
    offset += static_cast<size_t>(config.maxDependencies) * sizeof(DependencyEdge);

    offset = alignUp(offset, alignof(uint32_t));
    offset += static_cast<size_t>(config.maxJobs) * sizeof(uint32_t); // readyQueue_

    offset = alignUp(offset, alignof(uint32_t));
    offset += static_cast<size_t>(config.maxJobs) * sizeof(uint32_t); // slotFreeList_

    offset = alignUp(offset, alignof(uint32_t));
    offset += static_cast<size_t>(config.maxDependencies) * sizeof(uint32_t); // edgeFreeList_

    return offset;
}

JobSystem::JobSystem(std::span<std::byte> storage, const JobSystemConfig& config)
    : maxJobs_(config.maxJobs), maxDependencies_(config.maxDependencies) {
    ENG_ASSERT(storage.size() >= requiredBytes(config),
               "Insufficient storage provided for JobSystem");
    ENG_ASSERT(isAligned(storage.data(), requiredAlignment()),
               "Storage not properly aligned for JobSystem");
    ENG_ASSERT(maxJobs_ > 0, "maxJobs must be greater than zero");
    ENG_ASSERT(maxDependencies_ > 0, "maxDependencies must be greater than zero");

    auto* base = storage.data();
    size_t offset = 0;

    offset = alignUp(offset, alignof(JobSlot));
    jobs_ = reinterpret_cast<JobSlot*>(base + offset);
    offset += static_cast<size_t>(maxJobs_) * sizeof(JobSlot);

    offset = alignUp(offset, alignof(DependencyEdge));
    edges_ = reinterpret_cast<DependencyEdge*>(base + offset);
    offset += static_cast<size_t>(maxDependencies_) * sizeof(DependencyEdge);

    offset = alignUp(offset, alignof(uint32_t));
    readyQueue_ = reinterpret_cast<uint32_t*>(base + offset);
    offset += static_cast<size_t>(maxJobs_) * sizeof(uint32_t);

    offset = alignUp(offset, alignof(uint32_t));
    slotFreeList_ = reinterpret_cast<uint32_t*>(base + offset);
    offset += static_cast<size_t>(maxJobs_) * sizeof(uint32_t);

    offset = alignUp(offset, alignof(uint32_t));
    edgeFreeList_ = reinterpret_cast<uint32_t*>(base + offset);
    offset += static_cast<size_t>(maxDependencies_) * sizeof(uint32_t);
    ENG_ASSERT(offset <= storage.size(), "JobSystem layout exceeded storage bounds");

    for (uint32_t i = 0; i < maxJobs_; ++i) {
        new (&jobs_[i]) JobSlot();
        slotFreeList_[i] = maxJobs_ - 1 - i;
    }
    freeSlotCount_ = maxJobs_;

    for (uint32_t i = 0; i < maxDependencies_; ++i) {
        new (&edges_[i]) DependencyEdge();
        edgeFreeList_[i] = maxDependencies_ - 1 - i;
    }
    freeEdgeCount_ = maxDependencies_;

    if (config.workerCount > 0) {
        (void)initializeWorkers(config.workerCount);
    }
}

JobSystem::~JobSystem() {
    shutdown();
    for (uint32_t i = 0; i < maxJobs_; ++i) {
        jobs_[i].~JobSlot();
    }
    for (uint32_t i = 0; i < maxDependencies_; ++i) {
        edges_[i].~DependencyEdge();
    }
}

Result<void, Error> JobSystem::initializeWorkers(uint32_t workerCount) noexcept {
    if (workerCount > kMaxSupportedWorkers) {
        return Result<void, Error>::failure(
            Error(ErrorCode::InvalidArgument, "workerCount exceeds maximum supported workers"));
    }

    std::unique_lock lock(mutex_);
    if (workerCount_ > 0) {
        return Result<void, Error>::failure(
            Error(ErrorCode::InvalidState, "Workers already running; call shutdown() first"));
    }

    stopping_.store(false, std::memory_order_release);
    workerCount_ = workerCount;
    for (uint32_t i = 0; i < workerCount_; ++i) {
        workers_[i] = std::thread(&JobSystem::workerLoop, this, i);
    }
    return Result<void, Error>::success();
}

void JobSystem::shutdown() noexcept {
    {
        std::unique_lock lock(mutex_);
        if (stopping_.load(std::memory_order_acquire) && workerCount_ == 0) {
            return;
        }
        stopping_.store(true, std::memory_order_release);

        for (uint32_t i = 0; i < maxJobs_; ++i) {
            JobState s = jobs_[i].state.load(std::memory_order_acquire);
            if (s == JobState::Pending || s == JobState::Ready) {
                jobs_[i].state.store(JobState::Canceled, std::memory_order_release);
            }
        }
        readyCount_ = 0;
        readyHead_ = 0;
        readyTail_ = 0;
    }

    cv_.notify_all();
    completionCv_.notify_all();

    for (uint32_t i = 0; i < workerCount_; ++i) {
        if (workers_[i].joinable()) {
            workers_[i].join();
        }
    }
    workerCount_ = 0;
}

Result<JobHandle, Error> JobSystem::submit(const JobDesc& desc) noexcept {
    if (desc.function == nullptr && desc.voidFunction == nullptr) {
        return Result<JobHandle, Error>::failure(
            Error(ErrorCode::InvalidArgument, "Job function must not be null"));
    }
    if (desc.dependencies.size() > kMaxInlinePredecessors) {
        return Result<JobHandle, Error>::failure(
            Error(ErrorCode::InvalidArgument, "Exceeded maximum dependencies per job"));
    }

    for (size_t i = 0; i < desc.dependencies.size(); ++i) {
        for (size_t j = i + 1; j < desc.dependencies.size(); ++j) {
            if (desc.dependencies[i] == desc.dependencies[j]) {
                return Result<JobHandle, Error>::failure(Error(
                    ErrorCode::InvalidArgument, "Duplicate dependency handle in job declaration"));
            }
        }
    }

    std::unique_lock lock(mutex_);

    if (stopping_.load(std::memory_order_acquire)) {
        return Result<JobHandle, Error>::failure(
            Error(ErrorCode::InvalidState, "Cannot submit jobs to a stopped JobSystem"));
    }

    for (const auto& dep : desc.dependencies) {
        if (!dep.isValid() || !isValidHandleLocked(dep)) {
            return Result<JobHandle, Error>::failure(
                Error(ErrorCode::HandleResolutionFailed, "Invalid or stale dependency handle"));
        }
    }

    if (freeSlotCount_ == 0) {
        reclaimCompletedSlotsLocked();
        if (freeSlotCount_ == 0) {
            return Result<JobHandle, Error>::failure(
                Error(ErrorCode::OutOfMemory, "Job capacity exhausted"));
        }
    }

    uint32_t neededEdges = 0;
    for (const auto& dep : desc.dependencies) {
        JobState s = jobs_[dep.index()].state.load(std::memory_order_acquire);
        if (s != JobState::Succeeded && s != JobState::Failed && s != JobState::Canceled) {
            ++neededEdges;
        }
    }

    if (neededEdges > freeEdgeCount_) {
        return Result<JobHandle, Error>::failure(
            Error(ErrorCode::OutOfMemory, "Dependency edge capacity exhausted"));
    }

    uint32_t slotIndex = slotFreeList_[--freeSlotCount_];
    JobSlot& slot = jobs_[slotIndex];

    slot.function = desc.function;
    slot.voidFunction = desc.voidFunction;
    slot.context = desc.context;
    slot.predecessorCount = 0;
    slot.firstSuccessorEdge = kInvalidIndex;

    JobHandle handle(slotIndex, slot.generation);

    uint32_t unresolved = 0;
    bool anyFailed = false;

    for (const auto& dep : desc.dependencies) {
        uint32_t depIndex = dep.index();
        slot.predecessors[slot.predecessorCount++] = depIndex;
        JobState depState = jobs_[depIndex].state.load(std::memory_order_acquire);

        if (depState == JobState::Failed || depState == JobState::Canceled) {
            anyFailed = true;
        } else if (depState != JobState::Succeeded) {
            uint32_t edgeIndex = edgeFreeList_[--freeEdgeCount_];
            edges_[edgeIndex].successorJobIndex = slotIndex;
            edges_[edgeIndex].nextEdgeIndex = jobs_[depIndex].firstSuccessorEdge;
            jobs_[depIndex].firstSuccessorEdge = edgeIndex;
            ++unresolved;
        }
    }

    if (anyFailed) {
        slot.state.store(JobState::Failed, std::memory_order_release);
        slot.unresolvedDependencies.store(0, std::memory_order_release);
        completionCv_.notify_all();
    } else if (unresolved == 0) {
        slot.state.store(JobState::Ready, std::memory_order_release);
        slot.unresolvedDependencies.store(0, std::memory_order_release);
        pushReadyQueueLocked(slotIndex);
        cv_.notify_one();
    } else {
        slot.state.store(JobState::Pending, std::memory_order_release);
        slot.unresolvedDependencies.store(unresolved, std::memory_order_release);
    }

    return Result<JobHandle, Error>::success(handle);
}

Result<JobHandle, Error> JobSystem::submit(JobFunction fn, void* ctx,
                                           std::span<const JobHandle> deps) noexcept {
    return submit(JobDesc(fn, ctx, deps));
}

Result<JobHandle, Error> JobSystem::submit(VoidJobFunction fn, void* ctx,
                                           std::span<const JobHandle> deps) noexcept {
    return submit(JobDesc(fn, ctx, deps));
}

Result<void, Error> JobSystem::addDependency(JobHandle target, JobHandle dependency) noexcept {
    if (!target.isValid() || !dependency.isValid()) {
        return Result<void, Error>::failure(
            Error(ErrorCode::InvalidArgument, "Invalid handle sentinel passed to addDependency"));
    }

    std::unique_lock lock(mutex_);

    if (!isValidHandleLocked(target) || !isValidHandleLocked(dependency)) {
        return Result<void, Error>::failure(
            Error(ErrorCode::HandleResolutionFailed, "Invalid or stale job handle"));
    }

    if (target.index() == dependency.index()) {
        return Result<void, Error>::failure(
            Error(ErrorCode::InvalidArgument, "Self-dependency cycle detected"));
    }

    JobSlot& targetSlot = jobs_[target.index()];
    JobState targetState = targetSlot.state.load(std::memory_order_acquire);
    if (targetState == JobState::Running || targetState == JobState::Succeeded ||
        targetState == JobState::Failed || targetState == JobState::Canceled) {
        return Result<void, Error>::failure(
            Error(ErrorCode::InvalidState, "Cannot add dependency to running or completed job"));
    }

    for (uint32_t i = 0; i < targetSlot.predecessorCount; ++i) {
        if (targetSlot.predecessors[i] == dependency.index()) {
            return Result<void, Error>::success();
        }
    }

    if (transitivelyDependsOnLocked(dependency.index(), target.index())) {
        return Result<void, Error>::failure(
            Error(ErrorCode::InvalidArgument, "Dependency cycle detected"));
    }

    if (targetSlot.predecessorCount >= kMaxInlinePredecessors) {
        return Result<void, Error>::failure(
            Error(ErrorCode::OutOfMemory, "Exceeded maximum dependencies per job"));
    }

    JobState depState = jobs_[dependency.index()].state.load(std::memory_order_acquire);
    targetSlot.predecessors[targetSlot.predecessorCount++] = dependency.index();

    if (depState == JobState::Failed || depState == JobState::Canceled) {
        targetSlot.state.store(JobState::Failed, std::memory_order_release);
        completionCv_.notify_all();
        return Result<void, Error>::success();
    }

    if (depState == JobState::Succeeded) {
        return Result<void, Error>::success();
    }

    if (freeEdgeCount_ == 0) {
        --targetSlot.predecessorCount;
        return Result<void, Error>::failure(
            Error(ErrorCode::OutOfMemory, "Dependency edge capacity exhausted"));
    }

    uint32_t edgeIndex = edgeFreeList_[--freeEdgeCount_];
    edges_[edgeIndex].successorJobIndex = target.index();
    edges_[edgeIndex].nextEdgeIndex = jobs_[dependency.index()].firstSuccessorEdge;
    jobs_[dependency.index()].firstSuccessorEdge = edgeIndex;

    targetSlot.unresolvedDependencies.fetch_add(1, std::memory_order_relaxed);
    targetSlot.state.store(JobState::Pending, std::memory_order_release);

    return Result<void, Error>::success();
}

Result<JobState, Error> JobSystem::wait(JobHandle handle) noexcept {
    if (!handle.isValid()) {
        return Result<JobState, Error>::failure(
            Error(ErrorCode::InvalidArgument, "Invalid job handle sentinel"));
    }

    while (true) {
        uint32_t jobToExecute = kInvalidIndex;
        {
            std::unique_lock lock(mutex_);

            if (handle.index() >= maxJobs_) {
                return Result<JobState, Error>::failure(
                    Error(ErrorCode::HandleResolutionFailed, "Job handle index out of bounds"));
            }

            const JobSlot& slot = jobs_[handle.index()];
            if (slot.generation != handle.generation()) {
                return Result<JobState, Error>::failure(
                    Error(ErrorCode::HandleResolutionFailed, "Stale job handle"));
            }

            JobState curState = slot.state.load(std::memory_order_acquire);
            if (curState == JobState::Succeeded || curState == JobState::Failed ||
                curState == JobState::Canceled) {
                return Result<JobState, Error>::success(curState);
            }

            if (stopping_.load(std::memory_order_acquire)) {
                return Result<JobState, Error>::success(curState);
            }

            if (readyCount_ > 0) {
                jobToExecute = readyQueue_[readyHead_];
                readyHead_ = (readyHead_ + 1) % maxJobs_;
                --readyCount_;
                jobs_[jobToExecute].state.store(JobState::Running, std::memory_order_release);
            } else {
                completionCv_.wait(lock, [&] {
                    if (stopping_.load(std::memory_order_acquire)) {
                        return true;
                    }
                    if (slot.generation != handle.generation()) {
                        return true;
                    }
                    JobState s = slot.state.load(std::memory_order_acquire);
                    return (s == JobState::Succeeded || s == JobState::Failed ||
                            s == JobState::Canceled || readyCount_ > 0);
                });
            }
        }

        if (jobToExecute != kInvalidIndex) {
            executeJob(jobToExecute);
        }
    }
}

Result<void, Error> JobSystem::waitAll() noexcept {
    while (true) {
        uint32_t jobToExecute = kInvalidIndex;
        {
            std::unique_lock lock(mutex_);
            if (activeJobCountLocked() == 0) {
                return Result<void, Error>::success();
            }
            if (stopping_.load(std::memory_order_acquire)) {
                return Result<void, Error>::success();
            }
            if (readyCount_ > 0) {
                jobToExecute = readyQueue_[readyHead_];
                readyHead_ = (readyHead_ + 1) % maxJobs_;
                --readyCount_;
                jobs_[jobToExecute].state.store(JobState::Running, std::memory_order_release);
            } else {
                completionCv_.wait(lock, [&] {
                    return stopping_.load(std::memory_order_acquire) ||
                           activeJobCountLocked() == 0 || readyCount_ > 0;
                });
            }
        }
        if (jobToExecute != kInvalidIndex) {
            executeJob(jobToExecute);
        }
    }
}

bool JobSystem::isComplete(JobHandle handle) const noexcept {
    if (!handle.isValid()) {
        return false;
    }
    std::unique_lock lock(mutex_);
    if (!isValidHandleLocked(handle)) {
        return false;
    }
    JobState s = jobs_[handle.index()].state.load(std::memory_order_acquire);
    return s == JobState::Succeeded || s == JobState::Failed || s == JobState::Canceled;
}

bool JobSystem::isSucceeded(JobHandle handle) const noexcept {
    if (!handle.isValid()) {
        return false;
    }
    std::unique_lock lock(mutex_);
    if (!isValidHandleLocked(handle)) {
        return false;
    }
    return jobs_[handle.index()].state.load(std::memory_order_acquire) == JobState::Succeeded;
}

bool JobSystem::isFailed(JobHandle handle) const noexcept {
    if (!handle.isValid()) {
        return false;
    }
    std::unique_lock lock(mutex_);
    if (!isValidHandleLocked(handle)) {
        return false;
    }
    return jobs_[handle.index()].state.load(std::memory_order_acquire) == JobState::Failed;
}

JobState JobSystem::state(JobHandle handle) const noexcept {
    if (!handle.isValid()) {
        return JobState::Unallocated;
    }
    std::unique_lock lock(mutex_);
    if (!isValidHandleLocked(handle)) {
        return JobState::Unallocated;
    }
    return jobs_[handle.index()].state.load(std::memory_order_acquire);
}

bool JobSystem::isValidHandle(JobHandle handle) const noexcept {
    if (!handle.isValid()) {
        return false;
    }
    std::unique_lock lock(mutex_);
    return isValidHandleLocked(handle);
}

bool JobSystem::isValidHandleLocked(JobHandle handle) const noexcept {
    if (handle.index() >= maxJobs_) {
        return false;
    }
    return jobs_[handle.index()].generation == handle.generation();
}

Result<void, Error> JobSystem::reclaim(JobHandle handle) noexcept {
    if (!handle.isValid()) {
        return Result<void, Error>::failure(
            Error(ErrorCode::InvalidArgument, "Invalid job handle sentinel"));
    }
    std::unique_lock lock(mutex_);
    if (handle.index() >= maxJobs_) {
        return Result<void, Error>::failure(
            Error(ErrorCode::HandleResolutionFailed, "Job handle index out of bounds"));
    }
    JobSlot& slot = jobs_[handle.index()];
    if (slot.generation != handle.generation()) {
        return Result<void, Error>::failure(
            Error(ErrorCode::HandleResolutionFailed, "Stale job handle"));
    }
    JobState s = slot.state.load(std::memory_order_acquire);
    if (s != JobState::Succeeded && s != JobState::Failed && s != JobState::Canceled) {
        return Result<void, Error>::failure(
            Error(ErrorCode::InvalidState, "Cannot reclaim job that is still active"));
    }

    uint32_t nextGen = slot.generation + 1;
    if (nextGen == GenerationalHandle::kInvalidGeneration || nextGen == 0xFFFF'FFFF) {
        nextGen = 1;
    }
    slot.generation = nextGen;
    slot.state.store(JobState::Unallocated, std::memory_order_release);
    slot.function = nullptr;
    slot.voidFunction = nullptr;
    slot.context = nullptr;
    slot.predecessorCount = 0;
    slotFreeList_[freeSlotCount_++] = handle.index();
    return Result<void, Error>::success();
}

uint32_t JobSystem::activeJobCount() const noexcept {
    std::unique_lock lock(mutex_);
    return activeJobCountLocked();
}

uint32_t JobSystem::activeJobCountLocked() const noexcept {
    uint32_t count = 0;
    for (uint32_t i = 0; i < maxJobs_; ++i) {
        JobState s = jobs_[i].state.load(std::memory_order_acquire);
        if (s == JobState::Pending || s == JobState::Ready || s == JobState::Running) {
            ++count;
        }
    }
    return count;
}

void JobSystem::workerLoop(uint32_t workerIndex) noexcept {
    (void)workerIndex;
    while (true) {
        uint32_t jobToExecute = kInvalidIndex;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock,
                     [&] { return stopping_.load(std::memory_order_acquire) || readyCount_ > 0; });

            if (stopping_.load(std::memory_order_acquire) && readyCount_ == 0) {
                break;
            }

            if (readyCount_ > 0) {
                jobToExecute = readyQueue_[readyHead_];
                readyHead_ = (readyHead_ + 1) % maxJobs_;
                --readyCount_;
                jobs_[jobToExecute].state.store(JobState::Running, std::memory_order_release);
            }
        }

        if (jobToExecute != kInvalidIndex) {
            executeJob(jobToExecute);
        }
    }
}

void JobSystem::executeJob(uint32_t jobIndex) noexcept {
    JobSlot& slot = jobs_[jobIndex];
    bool success = false;
    if (slot.function != nullptr) {
        success = slot.function(slot.context);
    } else if (slot.voidFunction != nullptr) {
        slot.voidFunction(slot.context);
        success = true;
    }
    finishJob(jobIndex, success);
}

void JobSystem::finishJob(uint32_t jobIndex, bool success) noexcept {
    std::unique_lock lock(mutex_);
    JobSlot& slot = jobs_[jobIndex];
    JobState finalState = success ? JobState::Succeeded : JobState::Failed;
    slot.state.store(finalState, std::memory_order_release);

    uint32_t edgeIndex = slot.firstSuccessorEdge;
    slot.firstSuccessorEdge = kInvalidIndex;

    while (edgeIndex != kInvalidIndex) {
        DependencyEdge edge = edges_[edgeIndex];
        edgeFreeList_[freeEdgeCount_++] = edgeIndex;

        uint32_t succIndex = edge.successorJobIndex;
        JobSlot& succSlot = jobs_[succIndex];

        if (!success) {
            std::array<uint32_t, 32> failStack{};
            uint32_t failTop = 0;
            succSlot.state.store(JobState::Failed, std::memory_order_release);
            failStack[failTop++] = succIndex;

            while (failTop > 0) {
                uint32_t failedJob = failStack[--failTop];
                JobSlot& fSlot = jobs_[failedJob];
                uint32_t fEdge = fSlot.firstSuccessorEdge;
                fSlot.firstSuccessorEdge = kInvalidIndex;
                while (fEdge != kInvalidIndex) {
                    DependencyEdge curEdge = edges_[fEdge];
                    edgeFreeList_[freeEdgeCount_++] = fEdge;
                    uint32_t downstreamJob = curEdge.successorJobIndex;
                    jobs_[downstreamJob].state.store(JobState::Failed, std::memory_order_release);
                    if (failTop < failStack.size()) {
                        failStack[failTop++] = downstreamJob;
                    }
                    fEdge = curEdge.nextEdgeIndex;
                }
            }
        } else {
            uint32_t prevUnresolved =
                succSlot.unresolvedDependencies.fetch_sub(1, std::memory_order_acq_rel);
            if (prevUnresolved == 1) {
                JobState curState = succSlot.state.load(std::memory_order_acquire);
                if (curState != JobState::Failed && curState != JobState::Canceled &&
                    !stopping_.load(std::memory_order_acquire)) {
                    succSlot.state.store(JobState::Ready, std::memory_order_release);
                    pushReadyQueueLocked(succIndex);
                    cv_.notify_one();
                }
            }
        }
        edgeIndex = edge.nextEdgeIndex;
    }

    completionCv_.notify_all();
}

void JobSystem::pushReadyQueueLocked(uint32_t jobIndex) noexcept {
    ENG_ASSERT(readyCount_ < maxJobs_, "Ready queue overflow in JobSystem");
    readyQueue_[readyTail_] = jobIndex;
    readyTail_ = (readyTail_ + 1) % maxJobs_;
    ++readyCount_;
}

void JobSystem::reclaimCompletedSlotsLocked() noexcept {
    for (uint32_t i = 0; i < maxJobs_; ++i) {
        JobState s = jobs_[i].state.load(std::memory_order_acquire);
        if ((s == JobState::Succeeded || s == JobState::Failed || s == JobState::Canceled) &&
            jobs_[i].firstSuccessorEdge == kInvalidIndex) {
            uint32_t nextGen = jobs_[i].generation + 1;
            if (nextGen == GenerationalHandle::kInvalidGeneration || nextGen == 0xFFFF'FFFF) {
                nextGen = 1;
            }
            jobs_[i].generation = nextGen;
            jobs_[i].state.store(JobState::Unallocated, std::memory_order_release);
            jobs_[i].function = nullptr;
            jobs_[i].voidFunction = nullptr;
            jobs_[i].context = nullptr;
            jobs_[i].predecessorCount = 0;
            slotFreeList_[freeSlotCount_++] = i;
        }
    }
}

bool JobSystem::transitivelyDependsOnLocked(uint32_t fromJobIndex,
                                            uint32_t targetJobIndex) const noexcept {
    if (fromJobIndex == targetJobIndex) {
        return true;
    }

    std::array<uint64_t, 64> visited{};
    std::array<uint32_t, 64> stack{};
    uint32_t stackTop = 0;

    stack[stackTop++] = fromJobIndex;
    visited[fromJobIndex / 64] |= (1ULL << (fromJobIndex % 64));

    while (stackTop > 0) {
        uint32_t curr = stack[--stackTop];
        const JobSlot& slot = jobs_[curr];
        for (uint32_t i = 0; i < slot.predecessorCount; ++i) {
            uint32_t pred = slot.predecessors[i];
            if (pred == targetJobIndex) {
                return true;
            }
            if (pred < 4096 && (visited[pred / 64] & (1ULL << (pred % 64))) == 0) {
                visited[pred / 64] |= (1ULL << (pred % 64));
                if (stackTop < stack.size()) {
                    stack[stackTop++] = pred;
                }
            }
        }
    }
    return false;
}

} // namespace eng
