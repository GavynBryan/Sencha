# Sensor overlap review and measurements

Reviewed PR #176 at `7e62f0cb397e72169bb53a956305c1512c555b75`.
The corrections retain its backend queries, World-owned observations, ordered
publication, bounds, and next-fixed-logic consumption contract. They add no
gameplay policy, event bus, worker lane, or second collision world.

## Finding dispositions

1. **Confirmed correctness defect.** Rigid-body displacement had no relocation
   contract; in particular `DrivenPoseTarget::Teleported` manufactured a crossing
   through skipped space. `TeleportBody` now resets the retained origin, while
   `SetBodyTransform` remains continuous. Both preserve occupancy comparison and
   physical motion after relocation. The per-body sampling method is private so
   callers cannot supply an origin that bypasses that contract. Character snap
   and restore already reset history; pool-level tests verify both. The current
   client reconciliation path is character-based; there is no replicated
   rigid-body reconciliation path to migrate. Future body corrections must use
   the explicit relocation API. See [the movement contract](sensor-overlaps.md#movement-and-relocation-contract).
2. **Confirmed evidence gap and unnecessary work.** The old benchmark measured
   only pair tracking. The new benchmark calls `SensorOverlapSystem::Physics`,
   including backend collection, ECS validation, and publication. A sensor count
   skips queries in empty sensor scenes. Existing Jolt layer filtering and
   `OnBody` metadata capture remove avoidable traversal/locking work. Sleeping
   queries remain: measured sparse costs are modest, and sensor relocation must
   still observe stationary bodies. No additional activity tracker is justified.
3. **Boundary retained.** `PhysicsWorld` is an engine integration facade, already
   sharing packed entity identity and character support identity with its ECS
   bridge. Query code reads Jolt body metadata, never the ECS World. ECS
   liveness/domain validation stays in `SensorOverlapSystem`; storage stays in
   the World. A new adapter would add a transfer without removing ECS traversal
   or backend exposure, because neither occurs in the existing query code.
4. **Ordering retained; two defects corrected.** Input propagation resolves this
   tick's fixed-logic/parent edits; output propagation resolves movement produced
   during physics. They cannot replace one another. Keeping input propagation
   at the physics boundary also serves standalone schedules. The added sensor
   pass incorrectly pushed authored poses into dynamic sensors and redundantly
   pushed kinematic sensors. It now handles only static sensors; the existing
   kinematic path reads world poses for parent support. A separate regression
   found 400 query-construction allocations across 200 warmed propagation
   sweeps. World-retained query caches remove these without changing scheduling
   or creating sensor-specific transform machinery.
5. **Unsupported existing contract, recorded as debt.** `Collider.IsTrigger`,
   shape, mesh, and most body configuration are captured at body creation.
   Structural reconciliation does not compare configuration values. Supplying
   configuration before initial binding is supported; mutating a resident collider
   is not. The follow-up is generic configuration reconciliation in `RigidBodyBinding`, due
   before runtime collider edits or Kyusu physics hot reload. See
   [deferred work](../deferred.md#runtime-collider-configuration-reconciliation).
6. **Existing guarantees retained.** Full generational identity, sorted pairs,
   Enter/Stay/Exit, transient Enter-then-Exit, initial occupancy, bounded storage,
   whole-sample overflow rejection, teardown forgetting, and owner-thread fixed
   publication remain unchanged. Queries execute no gameplay and grant no
   authority. New regressions exercise these through actual backend/schedule
   paths as well as the existing pure tracker tests.
7. **Nonlinear paths remain deferred.** Final-orientation linear casts do not
   reconstruct slides, angular trajectories, character substeps, or moving
   sensor sweeps. Relocations observe the final endpoint and movement since the
   last discontinuity, not intermediate destinations. These limits are explicit
   in [the contract](sensor-overlaps.md) and `docs/deferred.md`.

## Compatibility

`SetBodyTransform` keeps its original signature and continuous semantics;
`TeleportBody` and `SensorCount` are additive facade methods. The newly introduced
per-body observation method becomes private. `PhysicsWorld` and `CharacterMover`
retain their PIMPL layouts. No component, cooked scene, wire format, module
vtable or module descriptor layout changes. Header changes use the existing
generated SDK identity/fingerprint checks; engine and modules build together.
The owner-thread execution model is unchanged.

## Reproduction and method

```sh
SENCHA_BENCH_CPUS=2 bash scripts/bench_sensors.sh /tmp/sensors.json
```

The script configures the `profile` preset and builds `core_tests`. To reuse a
configured build, set `SENCHA_SKIP_BUILD=1`; `SENCHA_SENSOR_BUILD_DIR` overrides
its directory. `SENCHA_SENSOR_BENCH_REPS` defaults to 600;
`SENCHA_SENSOR_BENCH_CASE=rooms` selects one scenario. Every scenario runs at
60 and 120 Hz, with 180 warm-up ticks. `Bench::Recorder` writes schema-1 JSON;
raw measurements accompany this document. Debug numbers must not be compared
with these Release-codegen results.

Workstation: Intel Core i7-13620H, GCC 15.2.1, Linux x86-64, one process pinned to
performance CPU 2, `-O3 -DNDEBUG -g -fno-omit-frame-pointer`, Jolt's existing
single-threaded backend. Timing runs had no concurrent builds or profilers.
They are observations on this workstation, not platform-independent maxima.

The baseline backend is the original implementation plus query counters
(commit `7b03e1d7`), and the same benchmark fixture is used for both sides. To
reproduce it in a separate checkout at that commit, copy only
`test/core/SensorObservationBench.cpp` from the reviewed final head before
building. No runtime optimization is required by the harness. The review used
the baseline shared library with that harness via `LD_LIBRARY_PATH`; its symbol
set and loader resolution were checked before running.

The total observation measurement surrounds the real system call. Separate
attribution passes time rigid-body collection, character collection, and tracker
commit on another advanced tick. Those include Jolt broadphase, narrowphase,
collector work and body snapshots; they do not pretend to isolate only the
narrowphase algorithm. Query counters report actual overlap/cast calls, and
moving cases assert their expected casts so stationary characters cannot
silently stand in for moving ones. Physics/sync timings are recorded separately
and exclude gameplay, post-physics transform propagation and rendering. Neither
measurement is a claim about the entire engine frame.

All scenarios use 32,768 observation slots to admit the dense stress sample;
normal production default remains 4,096. Dense cases deliberately exceed that
default: 64 coincident large sensors retain 5,632 pairs, and casts add duplicate
hits. They measure output density rather than ordinary room occupancy. Sparse
cases use a 3 m actor grid. The room fixture uses 32 participating storage
partitions with 16 m rooms, floors, walls and perimeter props (1,024 static
shapes), 32 sensors, 256 moving dynamic bodies and 16 moving characters.

## Allocation and query attribution

Across all timed scenarios, the observation call made zero C++ allocations after
warm-up. The benchmark separately records allocations in physics/synchronization:
reconstructing propagation queries previously made 3–5 allocations per tick in
these fixtures; retaining queries reduces this to zero. The focused propagation
regression covers parented simulation and presentation sweeps as well.

The C++ counter does not intercept Jolt's malloc-based allocator. Heaptrack was
therefore run separately with the same scenarios (10 measured repetitions plus
the normal warm-up). No allocation backtraces matched `ObserveSensorShape` or
`SensorOverlapSystem::Physics`. As a control, the trace did resolve Jolt body
creation allocations under `PhysicsWorld::AddBody`. Fixture creation and whole
process allocation counts are nonzero; this claim concerns observation, not
arbitrary simulation or initialization.

```sh
SENCHA_SENSOR_BENCH_REPS=10 SENCHA_SENSOR_BENCH_OUT=/tmp/sensor-heap.json \
  heaptrack --record-only -o /tmp/sensor-heap build-profile/test/core_tests \
  --gtest_filter=SensorObservationBench.Generate
heaptrack_print -f /tmp/sensor-heap.zst --filter-bt-function ObserveSensorShape
heaptrack_print -f /tmp/sensor-heap.zst --filter-bt-function 'SensorOverlapSystem::Physics'
```

A separate CPU profile of `dense_moving` attributed 86.4% of sampled query-path
cycles to Jolt's `NarrowPhaseQuery::CollideShape` call tree and 13.5% to
`CastShape`. This includes their broadphase and collectors, rather than only
contact geometry arithmetic. Deep convex penetration work dominates the overlap
path. Sampling used frame pointers throughout the engine and Jolt (99.95% of
weighted samples retained the benchmark root), avoiding truncated deep-stack
attribution. These percentages explain the stress case; they are not independent
wall-clock timers or ratios promised for other scenes. Profiled runs are excluded
from the timing tables.

```sh
SENCHA_SENSOR_BENCH_CASE=dense_moving SENCHA_SENSOR_BENCH_REPS=50 \
SENCHA_SENSOR_BENCH_OUT=/tmp/sensor-profile.json \
  taskset -c 2 perf record -e cpu_core/cycles/u -F 999 --call-graph fp \
  -o /tmp/sensor.data -- build-profile/test/core_tests \
  --gtest_filter=SensorObservationBench.Generate
perf report -i /tmp/sensor.data --stdio --no-inline --children
```

On non-hybrid CPUs, use the available userspace cycle event in place of
`cpu_core/cycles/u`. Scope counters and staged timings do not require perf.

## Timing results

All table times are microseconds per observation tick. Raw schema-1 metrics:
[baseline](benchmarks/sensor-observations-baseline.json),
[corrected](benchmarks/sensor-observations-after.json). Query counts are
endpoint overlaps / casts in the corrected system. Sleeping counts were checked
in the backend (all bodies in the sleeping rows slept). The room rows were
recorded separately after expanding that fixture to actual partitioned rooms;
all other rows use the same 600-sample runs across the original matrix.

| Scenario | Hz | Baseline median | Corrected median | Corrected p95 | Corrected worst | Queries/tick |
|---|---:|---:|---:|---:|---:|---:|
| 0 sensors, 2,048 sleeping bodies | 60 | 295.94 | 32.76 | 41.82 | 48.07 | 0 / 0 |
| 0 sensors, 2,048 sleeping bodies | 120 | 263.89 | 32.99 | 37.08 | 50.47 | 0 / 0 |
| 0 sensors, 2,048 moving dynamics | 60 | 719.37 | 32.65 | 44.06 | 64.19 | 0 / 0 |
| 0 sensors, 2,048 moving dynamics | 120 | 636.72 | 32.74 | 36.55 | 45.62 | 0 / 0 |
| 0 sensors, 128 moving characters | 60 | 11.57 | 0.46 | 0.74 | 5.39 | 0 / 0 |
| 0 sensors, 128 moving characters | 120 | 7.44 | 0.46 | 0.49 | 0.69 | 0 / 0 |
| 4 sensors, 2,048 sleeping bodies | 60 | 292.81 | 122.67 | 179.68 | 348.23 | 2048 / 0 |
| 4 sensors, 2,048 sleeping bodies | 120 | 293.61 | 122.18 | 173.84 | 285.99 | 2048 / 0 |
| 4 sensors, 2,048 stationary kinematics | 60 | 291.81 | 123.93 | 190.81 | 223.79 | 2048 / 0 |
| 4 sensors, 2,048 stationary kinematics | 120 | 293.26 | 123.37 | 131.43 | 177.19 | 2048 / 0 |
| 4 sensors, 2,048 moving dynamics | 60 | 687.44 | 214.48 | 326.60 | 365.51 | 2048 / 2048 |
| 4 sensors, 2,048 moving dynamics | 120 | 692.08 | 211.57 | 225.02 | 281.77 | 2048 / 2048 |
| 4 sensors, 2,048 moving kinematics | 60 | 687.18 | 211.13 | 320.48 | 358.15 | 2048 / 2048 |
| 4 sensors, 2,048 moving kinematics | 120 | 690.19 | 209.74 | 222.26 | 399.40 | 2048 / 2048 |
| 512 sensors, 32 sleeping bodies | 60 | 103.05 | 101.89 | 158.04 | 215.17 | 32 / 0 |
| 512 sensors, 32 sleeping bodies | 120 | 103.00 | 100.93 | 108.34 | 195.82 | 32 / 0 |
| 256 sensors, 1,024 moving dynamics | 60 | 701.80 | 519.12 | 1018.15 | 1492.52 | 1024 / 1024 |
| 256 sensors, 1,024 moving dynamics | 120 | 724.65 | 519.13 | 989.59 | 1016.46 | 1024 / 1024 |
| 64 dense sensors, 128 sleeping bodies | 60 | 6547.21 | 6506.83 | 7510.89 | 12699.00 | 128 / 0 |
| 64 dense sensors, 128 sleeping bodies | 120 | 6553.19 | 6498.96 | 7468.00 | 10311.70 | 128 / 0 |
| 64 dense sensors, 128 moving kinematics | 60 | 7826.94 | 7693.56 | 8018.45 | 8706.35 | 128 / 128 |
| 64 dense sensors, 128 moving kinematics | 120 | 7909.51 | 7805.62 | 8425.54 | 10254.40 | 128 / 128 |
| 256 sensors, 128 moving characters | 60 | 208.65 | 216.08 | 329.21 | 500.90 | 128 / 128 |
| 256 sensors, 128 moving characters | 120 | 224.58 | 223.40 | 315.43 | 373.23 | 128 / 128 |
| 32 rooms, 256 dynamics + 16 characters | 60 | 297.85 | 255.10 | 3233.90 | 3277.44 | 272 / 272 |
| 32 rooms, 256 dynamics + 16 characters | 120 | 290.15 | 246.73 | 258.65 | 275.19 | 272 / 272 |

The 60 Hz attribution medians below time separate collection/commit passes, so
they need not sum to the full-system median. Physics/sync includes body binding,
input transform propagation, physics step, writeback and character movement,
without the observation call. The raw files also contain all 120 Hz values.

| Scenario | Body collection | Character collection | Tracker commit | Physics/sync | Retained pairs |
|---|---:|---:|---:|---:|---:|
| 0 sensors, 2,048 sleeping bodies | 32.44 | 0.03 | 0.02 | 171.84 | 0 |
| 0 sensors, 2,048 moving dynamics | 31.98 | 0.06 | 0.11 | 640.69 | 0 |
| 0 sensors, 128 moving characters | 0.01 | 0.42 | 0.02 | 15.94 | 0 |
| 4 sensors, 2,048 sleeping bodies | 121.99 | 0.02 | 0.07 | 173.07 | 4 |
| 4 sensors, 2,048 stationary kinematics | 123.10 | 0.05 | 0.14 | 343.20 | 4 |
| 4 sensors, 2,048 moving dynamics | 211.32 | 0.07 | 0.22 | 650.59 | 4 |
| 4 sensors, 2,048 moving kinematics | 211.43 | 0.07 | 0.18 | 588.46 | 4 |
| 512 sensors, 32 sleeping bodies | 100.20 | 0.02 | 0.20 | 23.76 | 32 |
| 256 sensors, 1,024 moving dynamics | 492.02 | 0.07 | 2.58 | 602.66 | 256 |
| 64 dense sensors, 128 sleeping bodies | 5876.54 | 0.16 | 264.87 | 17.27 | 5632 |
| 64 dense sensors, 128 moving kinematics | 6620.14 | 0.33 | 562.33 | 6387.03 | 5632 |
| 256 sensors, 128 moving characters | 0.01 | 207.42 | 1.03 | 248.91 | 128 |
| 32 rooms, 256 dynamics + 16 characters | 209.40 | 27.65 | 1.97 | 238.12 | 144 |

The observation budget is 16.667 ms at 60 Hz and 8.333 ms at 120 Hz only if it
were allowed the *entire* tick; gameplay and simulation need their own share.
With four sensors and 2,048 moving bodies, corrected observation is about 1.3%
of a 60 Hz tick or 2.5% of a 120 Hz tick. Empty sensor scenes still sample poses
in about 33 us, issuing zero spatial queries. Retaining sleeping-body checks
costs about 123 us for 2,048 bodies and four sensors (0.7% / 1.5% of those budgets).
These costs do not justify a second activity/spatial tracker.

Output density and contact geometry remain material. The dense moving median
consumes about 46% of the 60 Hz budget and 94% of the 120 Hz budget; physics/sync
adds about another 6.4–6.5 ms in that fixture. Its observed worst exceeds an
entire 120 Hz tick. The partitioned room case also has a long tail
at 60 Hz: 0.255 ms median, 3.234 ms p95 and 3.277 ms worst, versus 0.247 / 0.259 /
0.275 ms at 120 Hz. The same long tail exists in the baseline. Its precise cause was not isolated
by this fixture; these step rates encounter different poses, and the result is
not evidence that 120 Hz is generally cheaper. A median cannot hide the observed
failure to maintain a small per-subsystem budget on every tick.

The corrected implementation is affordable in the measured sparse scenarios;
it is not a performance approval for thousands of simultaneous deep overlaps.
Content requiring that density must budget and profile it before shipping.
Skipping sleeping bodies would not remove geometric cost for moving actors and
would break moving-sensor occupancy without further invalidation state. No
second broadphase, per-frame allocation, approximate shape test, or parallel
scheduler was introduced to mask this measured limit.

## Verification and merge recommendation

- Debug configuration/build completed with warnings treated as errors; the
  profiling build used Release code generation.
- Both reproduced correctness failures were observed before correction:
  driven-pose teleport published two false transitions; an authored dynamic
  sensor pose write published Exit instead of Stay. Both pass after correction.
- All 30 sensor model/backend/schedule tests and three allocation regressions
  passed, including fast dynamic/kinematic traversal, direct and driven-pose
  teleport with movement afterward, character snap/restore, sensor relocation,
  sleeping actors, no-sensor history, destruction, index reuse, residency,
  backend overflow and sorted publication.
- Full serial CTest in the isolated PR checkout reported **100% tests passed,
  0 tests failed out of 6,102**, 334.58 seconds. The disabled identity-table print
  test and opt-in evidence generators were reported separately as not run.
  Networking/display fixtures ran with their required host access. Sensor
  performance generators were executed separately, rather than counted as
  executed by the default CTest run.
- Physics isolation, module ABI/layout and transform/streaming tests passed in
  that run. `git diff --check` and the benchmark shell syntax check passed.
- Original-head GitHub CI run `39165098380`: Linux failed configuration on missing
  Freetype before compiling; Windows was cancelled while installing LLVM before
  configuring/building. Neither is an implementation test failure or evidence of
  a passing platform build. Required checks for the updated head still need to
  complete successfully.

The corrected code is suitable for the generic observation foundation, with the
published geometry/density limits. Retain the existing layering and phase order.
Merge after required CI is restored and green; local passing tests do not replace
Windows verification. Runtime collider mutation is a separate generic binding
follow-up, due before live collider edits, and is not silently provided here.
The branch retains the prerequisite ancestry through #174 and #175; no rebase or
base-branch change is needed for these corrections.
