#pragma once

#include <cstdint>
#include <functional>

//=============================================================================
// JobSystem
//
// Frame-lane fork-join substrate. See docs/ecs/parallelization.md, Decision 1.
//
// Contract (asserted in debug builds where possible):
//   - The calling thread participates in job execution. A pool with zero
//     workers runs every job inline on the caller, in index order — that
//     configuration is the deterministic implementation for tests.
//   - ParallelFor must not be called from inside a job (no nesting), and at
//     most one ParallelFor may be active per pool at a time.
//   - Job callbacks must not throw. Debug builds log the job index and abort.
//   - Jobs must not touch ambient mutable engine state. Logging through an
//     already-resolved Logger is the sanctioned exception.
//=============================================================================
class JobSystem
{
public:
    virtual ~JobSystem() = default;

    // Pool worker threads only. The calling thread is not counted.
    virtual uint32_t WorkerCount() const = 0;

    // Stable index for the current thread: 0 = the forking caller,
    // 1..WorkerCount() = pool workers. Only valid inside a job callback;
    // callers use it to index per-worker scratch buffers, which therefore
    // need WorkerCount() + 1 slots.
    virtual uint32_t CurrentWorkerIndex() const = 0;

    // Blocking fork-join. Invokes fn(0) .. fn(jobCount - 1) exactly once
    // each, possibly concurrently, and returns only after all invocations
    // have completed. jobCount == 0 returns immediately.
    virtual void ParallelFor(uint32_t jobCount,
                             const std::function<void(uint32_t jobIndex)>& fn) = 0;
};
