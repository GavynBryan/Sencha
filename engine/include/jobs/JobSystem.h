#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

//=============================================================================
// JobSystem
//
// Frame-lane fork-join substrate. See docs/ecs/parallelization.md, Decision 1.
//
// Fixed-size worker pool. Workers park on a condition variable between
// batches; ParallelFor publishes one batch, wakes the pool, and the caller
// executes jobs from the same atomic counter until the batch is exhausted,
// then waits for stragglers.
//
// Batch state lives in a shared_ptr so a worker that wakes late -- after its
// batch already completed -- sees an exhausted counter on its own copy and
// goes back to sleep without ever touching state from a newer batch.
//
// Contract (asserted in debug builds where possible):
//   - The calling thread participates in job execution. JobSystem(0) runs
//     every job inline on the caller, in index order -- that configuration is
//     the deterministic reference path for tests.
//   - ParallelFor must not be called from inside a job (no nesting), and at
//     most one ParallelFor may be active per pool at a time.
//   - Job callbacks must not throw. Debug builds log the job index and abort.
//   - Jobs must not touch ambient mutable engine state. Logging through an
//     already-resolved Logger is the sanctioned exception.
//=============================================================================
class JobSystem
{
public:
    explicit JobSystem(uint32_t workerCount);
    ~JobSystem();

    // hardware_concurrency() - 2 (main thread and render path are already
    // occupied), clamped to 0 -- which degrades to caller-only execution on
    // small machines rather than oversubscribing them.
    [[nodiscard]] static uint32_t DefaultWorkerCount();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Pool worker threads only. The calling thread is not counted.
    [[nodiscard]] uint32_t WorkerCount() const
    {
        return static_cast<uint32_t>(Workers.size());
    }

    // Stable index for the current thread: 0 = the forking caller,
    // 1..WorkerCount() = pool workers. Only valid inside a job callback;
    // callers use it to index per-worker scratch buffers, which therefore
    // need WorkerCount() + 1 slots.
    [[nodiscard]] uint32_t CurrentWorkerIndex() const;

    // Blocking fork-join. Invokes fn(0) .. fn(jobCount - 1) exactly once
    // each, possibly concurrently, and returns only after all invocations
    // have completed. jobCount == 0 returns immediately.
    void ParallelFor(uint32_t jobCount,
                     const std::function<void(uint32_t jobIndex)>& fn);

private:
    struct Batch
    {
        const std::function<void(uint32_t)>* Fn = nullptr;
        uint32_t JobCount = 0;
        std::atomic<uint32_t> NextIndex{ 0 };
        std::atomic<uint32_t> CompletedCount{ 0 };
    };

    void WorkerMain(uint32_t workerIndex);

    // Caller-and-worker shared execution loop: pull indices until exhausted.
    void ExecuteBatch(Batch& batch);

    std::vector<std::thread> Workers;
    std::mutex Mutex;
    std::condition_variable WorkSignal;   // workers: a new batch is published
    std::condition_variable DoneSignal;   // caller: last job of a batch done
    std::shared_ptr<Batch> CurrentBatch;  // guarded by Mutex
    uint64_t Generation = 0;              // guarded by Mutex
    bool ShutdownRequested = false;       // guarded by Mutex

#ifndef NDEBUG
    std::atomic<bool> DebugParallelForActive{ false };
#endif
};
