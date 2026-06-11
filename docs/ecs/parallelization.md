# Parallel Execution Design (Jobs)

This document supersedes the original Phase 5 draft (preserved in git history). The
draft had three defects this revision fixes:

1. **Internal contradiction.** It sketched a single-active `ParallelFor` thread pool
   (Decision 1) while requiring the scheduler to run multiple systems concurrently
   (Decision 3). The interface could not express its own scheduler.
2. **Missing dimensions.** It modeled access conflicts per component *type*, but Sencha
   executes over `FrameRegistryView` — the global registry plus N disjoint zone
   registries. Conflicts are per (registry, component). It also ignored zones as a
   parallelism axis entirely, and said nothing about systems that *produce output*
   (render extraction appends to a queue; parallel chunk callbacks over a shared
   vector are a data race).
3. **Wrong target by its own numbers.** Phase 4 benchmarks: transform propagation
   ~209 ns/transform (~2 ms at 10k entities, excluded as a non-goal), render
   extraction ~0.614 ns/entity (~6 µs at 10k, the flagship use case). The plan
   parallelized the microsecond work and excluded the millisecond work.

The revised plan is staged so each stage is independently shippable, and the stages
are ordered by return-on-complexity: the substrate first, latency-hiding (async
loading) second, structural parallelism (zones) third, chunk-level data parallelism
last and gated on profiling.

## Goals

- One thread-pool substrate shared by all parallel work in the engine.
- Two execution lanes with distinct contracts:
  - **Frame lane**: fork-join work that completes inside the current phase.
  - **Async lane**: cross-frame tasks whose completion is observed at one defined
    main-thread point per frame (the pattern `SwapchainRebuildWorker` already uses).
- Preserve the `Read<T>` / `Write<T>` access model and ECS ownership rules from
  Phases 0–4 unchanged.
- Every stage gated by a benchmark or a concrete user-visible win.

## Non-goals

- No task graph. No stage below requires dependency modeling between jobs.
- No cross-system overlap (two systems running concurrently within a phase). This is
  explicitly deferred to a future stage and sketched in Appendix A so the eventual
  design is not hand-waved — but nothing below depends on it.
- No parallel transform propagation. Parent-before-child ordering makes it a separate
  design problem; it is also the only system where the win would currently be
  measurable, so when profiling justifies parallelism work, *that* design happens
  first, not more breadth here.
- No lock-free ECS mutation from worker threads. Workers never perform structural
  changes; `CommandBuffer` remains the only mutation channel and flushes remain
  main-thread, outside parallel sections.

## Decision 1: the substrate — `JobSystem`

```cpp
class JobSystem
{
public:
    virtual ~JobSystem() = default;

    // Pool worker threads only. The calling thread is not counted.
    virtual uint32_t WorkerCount() const = 0;

    // Stable index for the current thread: 0 = the forking caller,
    // 1..WorkerCount() = pool workers. Only valid inside a job callback
    // (callers use it to index per-worker scratch buffers, Decision 5).
    virtual uint32_t CurrentWorkerIndex() const = 0;

    // Blocking fork-join. Returns after all jobCount invocations complete.
    virtual void ParallelFor(uint32_t jobCount,
                             const std::function<void(uint32_t jobIndex)>& fn) = 0;
};
```

Contract (the draft left all of these unspecified; each is a real bug when omitted):

- **Caller participates.** The forking thread executes jobs from the same atomic
  counter alongside the workers. This means `ThreadPoolJobSystem(0)` — a pool with
  zero workers — *is* the deterministic single-threaded implementation for tests; no
  separate class needed, and behavior degrades gracefully rather than deadlocking.
- **No nesting.** Calling `ParallelFor` from inside a job asserts in debug builds
  (thread-local in-job flag). Nested fork-join on a fixed pool either deadlocks or
  silently serializes; we forbid it until something needs it.
- **One active `ParallelFor` per pool at a time**, asserted. This is what keeps the
  implementation an atomic next-index counter plus a completion count. Concurrent
  parallel-fors require a multi-queue scheduler; nothing in Stages A–D needs one.
- **Callbacks must not throw.** The debug implementation wraps each job, logs the
  job index, and aborts. The engine does not use exceptions for control flow and the
  join point has no sane recovery.
- **Jobs must not touch ambient mutable state** (asset caches, event bus, registries
  outside the data handed to them). The logging provider is the one ambient service
  jobs may call, and making it thread-safe is a Stage A deliverable.

Implementation: fixed pool sized `hardware_concurrency() - 2` by default (the main
thread and the render path are already occupied), parked on a condition variable,
atomic next-index, atomic completion count, caller spins on completion of the last
few jobs. Per-job overhead is one `fetch_add` plus an indirect call (~tens of ns);
the dominant fixed cost is waking parked workers — **measured at ~300 µs per
dispatch** on the dev machine (WSL2, 14 hardware threads, 12 workers; see Stage A
results below), which is why Stage D is profile-gated rather than assumed to win.

## Decision 2: ownership — the engine owns the pool, `World` stays data

Unchanged from the draft, with placement made concrete:

- `Engine` creates the concrete pool at startup and exposes it through the same
  service surface as other engine-owned singletons.
- `EngineSchedule` and systems receive `JobSystem&`; queries receive it per call
  (Decision 4). `World`, `Registry`, and `ZoneRuntime` never see it.
- Tests construct `ThreadPoolJobSystem(0)` for determinism.

## Decision 3: the async lane — `AsyncTaskQueue`

Long-running work (file IO, decode, future background computation) does not go
through `ParallelFor`. It gets a separate, smaller surface generalizing the
request/poll handshake `SwapchainRebuildWorker` already proved out:

```cpp
class AsyncTaskQueue
{
public:
    // `work` runs on a dedicated low-priority task thread (not the frame pool —
    // a level load must never starve frame jobs, and task threads block on IO).
    // `commit` runs on the main thread at the single drain point.
    TaskHandle Submit(std::function<TaskPayload()> work,
                      std::function<void(TaskPayload)> commit);

    bool IsComplete(TaskHandle) const;
    void Cancel(TaskHandle);   // best-effort: drops commit if not yet drained

    // Called exactly once per frame on the main thread, immediately after
    // RuntimeFrameLoop::BeginFrame and before any fixed tick. This is the only
    // place commit callbacks run, so commits may mutate anything the main
    // thread may mutate — including zone lifecycle, which the frame contract
    // already forbids mid-frame.
    void DrainCompletions(std::size_t maxCommitsPerFrame = SIZE_MAX);

    // Zero-thread test mode (mirrors ThreadPoolJobSystem(0)): constructed with
    // no task threads, Submit only enqueues; PumpWork runs pending work inline
    // on the calling thread. Submit → PumpWork → DrainCompletions is then fully
    // deterministic, with no sleeps or polling in tests.
    void PumpWork(std::size_t maxTasks = SIZE_MAX);
};
```

Why a separate lane instead of priorities on one pool:

- Task threads block on IO; frame workers must never block. Mixing them means either
  priority machinery or starvation. Two task threads is plenty for the foreseeable
  workload.
- The drain point gives async work a single, auditable reentry into engine state.
  Everything the worker produced is plain data until `commit` runs; there is no
  moment where two threads see a half-published result. This is the same
  publish-by-handoff model the ECS uses (command buffers) and the swapchain worker
  uses (`Poll` consumes `Ready` once).

`SwapchainRebuildWorker` stays as-is; folding it in is allowed later but is not a
goal — it predates this design and works.

### First consumer: async zone loading

The proof workload for the async lane, and the only currently known frame-time
problem jobs actually solve (a zone load today is a synchronous hitch):

1. `Submit` work: read + parse the scene, resolve asset payloads to CPU-side staging
   data (decoded images, mesh buffers), and build entities into a **detached
   `Registry`** owned solely by the task. Disjoint registries are the engine's
   isolation primitive — the worker needs no locks because nothing else can reach
   the object.
2. `commit`: insert staged assets into the caches and perform GPU uploads (the
   caches and Vulkan services stay single-threaded in v1 — decode is the expensive
   part; upload on the main thread is the cheap remainder and can move to a transfer
   queue later if profiles demand), attach the registry via `ZoneRuntime`, and fire
   `MarkTemporalDiscontinuity(TemporalDiscontinuityReason::ZoneLoad)` — the enum
   value has been waiting for this since the frame loop was written.
3. Zone *unloading* follows the existing rule: runtime Vulkan destruction goes
   through the deletion queue.

`maxCommitsPerFrame` exists for this consumer: commits do GPU uploads, so the drain
can be budgeted to avoid trading a load hitch for an upload hitch.

## Decision 4: frame lane, axis 1 — zone-level parallelism

The draft's omission with the best cost/benefit ratio. Systems today receive a
`FrameRegistryView` and loop its registry spans serially. Disjoint registries cannot
alias **by construction** — no access metadata, no declarations, no scheduler
changes:

```cpp
// before                                  // after
for (Registry* r : view.Logic)             jobs.ParallelFor(view.Logic.size(),
    RunOn(*r);                                 [&](uint32_t i) { RunOn(*view.Logic[i]); });
```

Rules:

- The job for registry *i* touches only registry *i*. The global registry is
  read-only inside the parallel span; systems that write global state do so before
  the fork or after the join, on the calling thread.
- Command buffers are per-registry, recorded inside the job, flushed serially after
  the join on the calling thread. Recording is allocation-only and touches no shared
  state; flushing is the structural mutation and stays main-thread.
- This composes with Decision 5: a chunk-parallel sweep inside a zone-parallel job
  would be nesting, which Decision 1 forbids. Pick the axis that matches the
  workload shape; zone-parallel for many-zones, chunk-parallel for one fat registry.

This stage is only profitable when multiple populated zones are live, which is a
product question (streaming worlds, server-style simulation) — hence its position
*after* async loading, which creates multi-zone workloads in the first place.

## Decision 5: frame lane, axis 2 — chunk-level parallel queries

`Query` gains a parallel entry point; the serial surface is untouched:

```cpp
query.ForEachChunkParallel(jobs, [&](auto& view) { /* same ChunkView contract */ },
                           referenceFrame);
```

Semantics, all decided on the calling thread before the fork:

- Archetype matching and the `Changed<T>` pre-pass run serially on the caller,
  producing a flat scratch list of passing chunks. `ParallelFor` runs over that
  list; one job = one chunk. At 16 KB per chunk (~585 rows for a two-component
  archetype) per-job work is microseconds for any non-trivial callback, which is
  adequate grain for an atomic-counter pool; no batching heuristics in v1.
- Column-version bumps keep their serial semantics ("bumped once per chunk after
  the callback") and are safe because `Chunk::LastWrittenFrames` is per-chunk state
  and each chunk is owned by exactly one job. `World::CurrentFrame()` is read-only
  during the sweep.
- Structural safety is asserted, not assumed: debug builds capture the world's
  structural version at fork and assert it unchanged at join. Any command-buffer
  flush or entity move during a parallel sweep is a bug this catches immediately.

### Output collection

The draft promised "identical results to serial execution" while its target systems
(extraction, culling) *append to output queues* — impossible as specified. Two
sanctioned patterns, chosen per system:

1. **Per-worker buffers + merge** — `CurrentWorkerIndex()` indexes a scratch buffer;
   buffers concatenate after the join. Output *order* is nondeterministic; output
   *set* is not. Correct wherever a downstream sort exists — render extraction sorts
   by `SortKey` before submission, so this is its pattern.
2. **Per-chunk slots in chunk order** — the scratch chunk list's index addresses an
   output slot per chunk; concatenation after the join reproduces serial order
   exactly. For consumers with no downstream sort.

A system that wants parallel sweep but fits neither pattern is a design smell —
it is doing reduction, and should say so and do a real two-pass reduction.

### Gate

Phase 4 numbers say this stage has no current customer: extraction is ~6 µs at 10k
entities, below one pool wake. The trigger to build Stage D is a profile showing a
single system's serial chunk sweep at or above **~1 ms** — roughly 100k+ entities of
extraction-weight work, or a new per-entity system (AI scoring is the expected first
real customer) that is chunk-pure and embarrassingly parallel.

## Testing strategy

The design splits cleanly into deterministic logic and a small concurrent core, and
the test plan follows that split (harness: the existing GoogleTest suites):

- **Logic, deterministically.** `ThreadPoolJobSystem(0)` and zero-thread
  `AsyncTaskQueue` + `PumpWork` make every consumer — parallel queries, output
  collection, zone-parallel loops, drain budgeting, cancellation — single-threaded
  gtest cases. The Stage C/D "identical results" criteria are direct comparisons
  against these configurations.
- **Contracts, via death tests.** Nesting, single-active `ParallelFor`, and
  structural mutation during a parallel sweep are `EXPECT_DEATH` cases — the
  pattern `test/ecs/EcsTests.cpp` already uses for ECS invariants.
- **The concurrent core, via stress + sanitizers.** The pool's wake/join/counter
  logic gets property tests (every index runs exactly once, join never returns
  early, edge cases at jobCount 0 / 1 / below and far above worker count, iterated)
  plus a ThreadSanitizer build configuration running the full suite. The threaded
  `AsyncTaskQueue` path gets one request/poll smoke test, same shape as the
  existing `SwapchainRebuildWorker` coverage in
  `test/runtime/FrameLoopScenarioTests.cpp`.

## Rollout

- **Stage A — substrate.** `JobSystem` + `ThreadPoolJobSystem` per Decision 1;
  thread-safe logging provider; contract asserts (nesting, single-active, in-job
  structural-version checks ride in Stage D). Benchmark: `ParallelFor` overhead
  curve (jobCount × job-size grid) so later gates have a measured floor.
- **Stage B — async lane + zone loading.** `AsyncTaskQueue` per Decision 3, drain
  point wired into the frame loop, async zone load end-to-end. Success: loading a
  zone produces zero missed fixed ticks on the main thread.
- **Stage C — zone-parallel system execution.** Convert registry-span loops in
  systems that qualify under Decision 4's rules. Gated on a multi-zone workload
  existing. Success: identical simulation results vs. `ThreadPoolJobSystem(0)`.
- **Stage D — chunk-parallel queries.** `ForEachChunkParallel` + both output
  patterns + fork/join structural asserts. Gated on the ~1 ms profile trigger.
  Success: identical results vs. serial for a per-entity system and (post-sort) for
  extraction, and measured speedup ≥ 2× on the triggering workload at 4 workers.

Each stage lands with the single-threaded configuration as the default for tests
and a flag to force it engine-wide for bisecting threading bugs.

### Stage A measured results (2026-06-11, WSL2, g++-14 -O3, 14 HW threads)

Substrate landed: `jobs/JobSystem.h`, `jobs/ThreadPoolJobSystem.{h,cpp}`,
thread-safe log sinks, 16 tests in `test/jobs/JobSystemTests.cpp` (clean under
`SENCHA_ENABLE_TSAN=ON`), overhead benchmark in `example/JobSystemBenchmark`.

From the benchmark (12 workers vs. `ThreadPoolJobSystem(0)`, medians of 51 runs):

- **Dispatch floor: ~300 µs** per `ParallelFor` — condvar wake of parked workers
  under the WSL2 scheduler dominates everything else. Two orders of magnitude
  above the per-job `fetch_add` cost.
- Break-even is roughly **1 ms of serial batch work**: 512 jobs × ~1 µs ran 3.3×
  faster; 4096 × ~10 µs peaked at ~7–8×. Below ~0.5 ms of total work, parallel
  dispatch is a slowdown.
- Consequence: the Stage D ~1 ms gate is confirmed, not loosened — at Phase 4's
  recorded extraction cost (~6 µs/10k entities) parallel dispatch would be a
  ~50× *regression*. Stages B and C remain the only justified consumers until
  entity counts grow by an order of magnitude. If the floor matters later,
  spinning workers briefly before parking is the first lever, not a task graph.

## Success criteria (revised for honesty)

- Safety properties are **asserted in debug builds** — single-active parallel-for,
  no nesting, structural version stable across fork/join — and *documented as
  contracts* in release. The draft claimed conflicts would be "structurally
  prevented in release"; no declaration-based scheme can promise that, and this
  design does not pretend to. What release builds structurally prevent is exactly
  what the data model prevents: jobs own disjoint chunks (Stage D) or disjoint
  registries (Stage C).
- `ThreadPoolJobSystem(0)` is bit-identical to pre-jobs behavior everywhere.
- No stage ships without its gate met and its benchmark recorded next to the
  Phase 4 numbers.

## Appendix A: cross-system batching (deferred, sketched so it isn't hand-waved)

Running multiple systems concurrently within a phase is the one feature that needs
real machinery, and nothing above needs it. If a future profile shows a phase
serialized on many small systems, the design is:

- Systems declare access sets at registration: a list of (registry-scope,
  `ComponentId`, read|write) plus a coarse `MainThreadOnly` bit for anything that
  touches ambient services. Conflict keys are **(registry, component)** pairs —
  per-type keys would falsely serialize same-type writes in different zones.
- Declarations are honor-system at registration but **verified at runtime in debug**:
  a thread-local "current system" context lets `Query` construction assert that the
  requested accessors are within the declared set. Undeclared ambient access cannot
  be verified mechanically; that is precisely why `MainThreadOnly` is the default
  a system must explicitly opt out of.
- Execution needs joinable task submission (a wait-group), not nested `ParallelFor`.
  That is an additive `JobSystem` extension, and the single-active-ParallelFor
  invariant is what a multi-queue implementation would replace.

Everything in this appendix is intentionally not scheduled. It exists so the next
reader knows the shape of the problem and why Stages A–D refused to pay for it.
