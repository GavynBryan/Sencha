# ECS Parallelization Design

This document scopes Phase 5 of the ECS migration: chunk-level parallel dispatch for
read-heavy systems without weakening the ownership and change-detection rules established
in Phases 0-4.

## Goals

- Parallelize query work at chunk granularity.
- Preserve the existing `Read<T>` / `Write<T>` access model.
- Keep the first implementation simple enough to debug and benchmark.
- Avoid introducing a fully general task graph before the engine has concrete need for it.

## Non-goals

- No speculative parallel transform propagation yet. The parent-before-child dependency
  still makes that a separate design problem.
- No lock-free ECS mutation from worker threads.
- No cross-system overlap for write-capable systems in the first version.

## Decision 1: job system interface

Phase 5 uses a simple thread-pool-backed `JobSystem` interface, not a task graph.

Proposed surface:

```cpp
class JobSystem
{
public:
    virtual ~JobSystem() = default;
    virtual uint32_t WorkerCount() const = 0;
    virtual void ParallelFor(uint32_t jobCount,
                             const std::function<void(uint32_t jobIndex)>& fn) = 0;
};
```

Why this shape:

- Chunk iteration is naturally an indexed `ParallelFor`.
- Work stealing can live inside the thread-pool implementation without leaking into ECS.
- A task graph would force dependency modeling before the scheduler actually needs it.

The first implementation can be a fixed-size thread pool with an atomic next-job index.
If profiling later shows scheduler overhead or dependency orchestration becoming the real
cost, a graph can be layered on top of the same interface.

## Decision 2: parallel query entry point

`Query` gains a parallel chunk API rather than changing the existing serial surface:

```cpp
query.ForEachChunkParallel(jobSystem, [&](auto& view) {
    // same view contract as ForEachChunk
});
```

Semantics:

- The query still resolves its matching archetype/chunk list on the calling thread.
- The chunk list is partitioned into jobs and executed in parallel.
- Each callback invocation still receives one chunk view.
- `Changed<T>` filtering remains a pre-pass done before dispatch.

This keeps the callback contract familiar and makes the parallel version a mechanical
upgrade for systems that are already chunk-pure.

## Decision 3: write-safety model

Two systems may not hold overlapping `Write<T>` access concurrently. Phase 5 enforces this
at the scheduler level, not with per-chunk locks.

Rules:

- A system running with any `Write<T>` access is exclusive with any other system that
  requests `Read<T>` or `Write<T>` on the same component type.
- Systems with read-only access may run in parallel with one another.
- Inside a single system, chunk-level parallelism is safe because each job owns disjoint
  chunks from the same query result.

Why scheduler-level exclusion:

- It reuses the access metadata we already have conceptually in queries.
- It keeps chunk iteration lock-free.
- It matches the migration’s conservative correctness bias.

This means the first scheduler can be simple:

1. Build a list of ready systems for the phase.
2. Batch together only systems whose access sets do not conflict.
3. Run each system, allowing that system to use `ForEachChunkParallel` internally.

## Decision 4: thread-pool ownership

The scheduler owns the `JobSystem`. `World` stays a pure data container.

Why:

- `World` should not become responsible for execution policy.
- Multiple worlds can be scheduled through the same runtime-owned worker pool.
- Tests can inject a single-threaded job system into the scheduler without changing ECS
  data structures.

Recommended placement:

- Runtime or engine services create the concrete thread pool.
- The ECS scheduler receives a `JobSystem&` during construction or phase execution.
- Systems and queries never own threads directly.

## Rollout plan

1. Add `JobSystem` abstraction plus a trivial single-threaded implementation for tests.
2. Add `Query::ForEachChunkParallel`.
3. Teach the scheduler to batch non-conflicting systems and pass the shared `JobSystem`.
4. Benchmark chunk-heavy read systems before and after enabling parallel dispatch.
5. Only then consider wider features like dependency graphs or async command-buffer flush.

## Success criteria

- Parallel query systems produce identical results to serial execution.
- Access conflicts remain impossible in debug builds and structurally prevented in release.
- The single-threaded job system preserves existing behavior for deterministic tests.
- Benchmarks show a clear gain on chunk-heavy read workloads before more scheduler
  complexity is added.
