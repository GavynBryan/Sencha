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
  design problem; at the time of writing it was also the only system where the win
  would be measurable. (Since superseded: D4.1 pointer caching plus dirty-subtree
  tracking took the steady-state sweep to ~6.6 ns/transform — verified 2026-06-11 —
  so there is currently **no** millisecond-scale system in the engine, and this
  non-goal costs nothing.)
- No lock-free ECS mutation from worker threads. Workers never perform structural
  changes; `CommandBuffer` remains the only mutation channel and flushes remain
  main-thread, outside parallel sections.

## Product shape (added 2026-06-11)

Sencha targets Metroidvania/Zelda-esque games: an interconnected world of
small, room-sized zones, streamed as the player moves — the current room plus
preloaded neighbors, so 2–4 live zones of hundreds of entities each, with
constant load/unload churn from backtracking. That shape re-grades the stages:

- **The async lane (Stage B) is the workhorse.** Seamless room transitions are
  the genre's core feel; background preloading via `AsyncZoneLoader` with
  dormant attaches (no discontinuity — see Decision 3) is the central engine
  capability, not a supporting one.
- **Zone-parallel execution (Stage C) is mechanism in waiting.** A whole
  room-shaped Logic span costs tens of microseconds — far below the ~300 µs
  dispatch floor — so the engine's Simulate phase propagates serially by
  default (`ZoneParallelPropagation = false`). The parallel overload, helper,
  and pool remain correct, tested, and one config flip away for workloads
  that clear the floor.
- **Chunk-parallel queries (Stage D) will likely never trigger.** Room-scale
  entity counts cannot produce a 1 ms serial chunk sweep. Do not build Stage D
  for this product on momentum; the gate stands.
- **These are defaults, not commitments.** The genre never enters the engine
  as an assumption — it enters as default values on mechanism that is itself
  shape-neutral. The dials, all in `EngineRuntimeConfig` and all tested in
  both positions: `ZoneParallelPropagation` (off; open worlds with many heavy
  zones turn it on), `AsyncTaskThreadCount` (1; multi-chunk streaming raises
  it), `JobWorkerCount` (auto; 0 is the serial bisect switch),
  `AsyncCommitBudgetMs` (2 ms). Policy that is not a number stays out of
  config entirely: preload/activation strategy is game code, and the
  dormant-attach rule is derived from participation, which is genre-neutral
  by construction — an open world streams dormant terrain chunks and flips
  them live exactly the way a Metroidvania streams rooms. What we refuse on
  purpose: genre-profile blobs and stringly-typed strategy selection.
- **The expected next consumer is unload-side.** Backtracking means rooms
  persist state when they unload (enemies, doors, pickups). When a save
  system exists, the clean design is a `DetachZone` inverse of `AttachZone`:
  move the registry out of the frame, hand it to an async task that owns it
  solely — serialization off-thread with zero locks, by the same
  publish-by-handoff symmetry the load path uses. Not built; no consumer yet.

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

    // Called exactly once per frame on the main thread, from the dedicated
    // FramePhase::DrainAsyncTasks (after RebuildGraphics, before ScheduleTicks
    // and any fixed tick). A real phase rather than an inline call so drains
    // show up in FrameTrace/TimingPanel like everything else. This is the only
    // place commit callbacks run, so commits may mutate anything the main
    // thread may mutate — including zone lifecycle, which the frame contract
    // already forbids mid-frame.
    void DrainCompletions(const AsyncDrainBudget& budget = {});

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
   value has been waiting for this since the frame loop was written. **Revision
   (product shape):** the discontinuity fires only for attaches with any
   participation. A dormant attach — the room-preload path — is invisible to
   every frame span, so no discontinuity occurred, definitionally; activation
   at the doorway is the game's own `SetParticipation` (plus `Teleport` if the
   camera cuts).
3. Zone *unloading* follows the existing rule: runtime Vulkan destruction goes
   through the deletion queue.

The drain budget exists for this consumer: commits do GPU uploads, so the drain
is budgeted to avoid trading a load hitch for an upload hitch. `AsyncDrainBudget`
has two axes with distinct semantics: `MaxCommits` is a hard cap (0 = explicit
pause; tests use small counts for deterministic stepping) and `MaxTime` is a
soft wall-time cap — the first ready commit always runs so completions cannot
starve, and no further commit starts once the budget is spent. The budget is
wall time because that is the unit the frame actually cares about; a commit
count is only a proxy and breaks as soon as commits stop being uniform. A
budget deliberately cannot split a commit (commits are atomic by design), so a
consumer with a large payload must shape it as multiple chunked tasks; the
budget then meters between chunks. The engine drains once per frame with
`EngineRuntimeConfig::AsyncCommitBudgetMs` (default 2 ms; 0 = unbudgeted,
following the TargetFps convention — the magic zero lives only at the config
boundary, never in the API).

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

### Stage C status (2026-06-11)

Landed, test-verified (683 tests green, TSan clean including parallel
propagation over live Worlds):

- `world/registry/RegistryParallel.h` — `ForEachRegistryParallel`, the one
  place the Decision 4 rules live. Spans of zero or one registries run inline
  on the caller (the dispatch floor is never paid for an unparallelizable
  span); larger spans fork one job per registry. Entries must be distinct.
- `Engine` now owns the frame pool (`ThreadPoolJobSystem`), sized by
  `EngineRuntimeConfig::JobWorkerCount` (-1 = auto `hardware_concurrency - 2`,
  0 = the engine-wide single-threaded bisect/determinism switch the rollout
  required, positive = pinned), exposed as `Engine::Jobs()`. Game systems
  reach it through their context's `EngineInstance` — no context surface
  growth until a game system actually wants it.
- Transform propagation — the only millisecond-scale system — converted:
  `PropagateTransforms(JobSystem&, span)` deduplicates, then runs one zone per
  job. Legal because the propagation order cache is a World resource and
  parent-before-child is an intra-zone constraint. The frame's Simulate phase
  now uses it.
- Render extraction deliberately not converted: it appends to a shared queue
  (the Decision 5 output-collection problem) and costs ~6 µs — both reasons
  independently disqualify it.

Measured (release, 12 workers, 10k entities/zone, roots dirtied per rep):

| zones | serial | zone-parallel | speedup |
|------:|-------:|--------------:|--------:|
| 1 | 0.124 ms | 0.125 ms | 1.00× (inline path) |
| 2 | 0.250 ms | 0.340 ms | 0.74× |
| 4 | 0.558 ms | 0.484 ms | 1.15× |
| 8 | 2.674 ms | 0.660 ms | 4.05× |

The 2-zone regression is the dispatch floor being honest: ~90 µs lost per
fixed tick at 2 light zones, ~2 ms gained at 8. We explicitly chose **not** to
hide a zone-count heuristic inside the helper — a count is a proxy for work,
the same mistake as commit-count budgets, and the right threshold depends on
per-zone cost the helper cannot know. The policy stays at the call site: a
consumer that knows its zones are light can call the serial overload, and
`JobWorkerCount = 0` turns the whole engine serial. If light-multi-zone scenes
become the common case in practice, revisit with profiles, not a constant.

**Revision (product shape, same day):** they are the common case — the target
genre streams 2–4 room-sized zones. The Simulate phase call site reverted to
the serial overload (the prediction above, settled by product knowledge rather
than a heuristic). The mechanism stays; the engine default now matches the
game it serves.

### Stage B status (2026-06-11)

Landed, test-verified end to end (671 tests green, TSan clean):

- `jobs/AsyncTaskQueue.{h,cpp}` — per-task atomic state machine
  (Pending → Running → AwaitingCommit → Committed, with Cancelled as the
  diversion); handles observe state via shared pointers, so the queue keeps no
  registry of finished tasks. `Submit<TPayload>` is templated; the payload is
  type-erased inside the work/commit thunk (`AsyncTaskHandle` replaces the
  sketch's `TaskHandle`).
- `ZoneRuntime::ReserveRegistryId` + `AttachZone` — the detached-build seam:
  the id is reserved on the main thread, the registry is built off-thread under
  sole task ownership, attach is a move.
- `zone/AsyncZoneLoader.{h,cpp}` — BeginLoad/IsLoading/CancelLoad; commit
  attaches the zone and fires the `ZoneLoad` discontinuity.
- `FramePhase::DrainAsyncTasks` — the drain point as a traced frame phase;
  `Engine` owns one `AsyncTaskQueue` (one task thread) created in Initialize.
- Tests: 16 queue tests + 10 zone-load tests, including zero-thread
  deterministic paths (`PumpWork`), threaded smoke tests, cancellation, and
  contract death tests.

First real adoption: CubeDemo loads its zone through `AsyncZoneLoader`. The
conversion drove one API addition — `BeginLoad` takes an optional main-thread
`finalize` callback (runs inside the commit, after attach, before the
discontinuity) because scene deserialization acquires from the asset caches
and therefore cannot run in the work stage. The split is the template for
future consumers: work = file IO + JSON parse + registry skeleton
(`InitializeDefault3DRegistry`, extracted from `CreateDefault3DZone`);
finalize = `LoadSceneJson` + camera/game-state wiring. The demo's systems and
debug panel null-check the registry pointer every tick and idle until the
commit flips it — the game runs normally while the zone loads.

Commit budgeting landed with the conversion: `AsyncDrainBudget` (hard commit
cap + soft wall-time cap with guaranteed progress), wired to
`EngineRuntimeConfig::AsyncCommitBudgetMs` at the drain phase. Deliberately
still out: asset staging through the async lane (no zone asset pipeline exists
yet — CubeDemo pre-registers procedural assets on the main thread before
submitting); when it arrives, large uploads must be shaped as chunked tasks so
the budget can meter them.

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
