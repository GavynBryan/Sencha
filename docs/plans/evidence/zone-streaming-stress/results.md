# Zone streaming under stress: scaling, cache behavior, and lifecycle safety

Measurements and findings for the world-graph and dock rework on
`agent/world-graphs-and-docks`. The question behind all of it: the partition's
manifest-level mechanisms are written with linear scans where a lookup would do,
and the code has only ever been driven at eight zones. Does that matter, and is
the lifecycle around streaming actually safe at the edges?

Short version: the load path's quadratic behavior is real and does not matter at
any plausible world size. The per-frame demand path had one genuine problem,
found by scaling rather than by reading — it cost the zone count times the
degree of whatever zone the focus was standing next to — and it is now fixed and
measured. Of the lifecycle hazards this pass set out to confirm, the two most
serious ones do not reproduce; both are guarded, and the guard is now written
down as a test rather than as an assumption.

## Method

- Machine: 13th Gen Intel Core i7-13620H, 6P+8E, 24 MiB L3, Linux 7.0.11, otherwise idle.
- Build: `profile` preset (`-O3 -DNDEBUG -g -fno-omit-frame-pointer`); every recorded
  number carries `build: release-codegen`. Debug numbers describe a different program.
- Pinning: `taskset` to the P-core list, via `scripts/bench_topology.sh`.
- Milliseconds are medians over batched repetitions; counts are exact. Comparisons are
  drift-normalized against `control_memory_stream_ms`, per the convention in
  `../unified-world-streaming/results.md`.
- Cache counters come from separate `perf stat -r 3` runs on the same scenario, user-mode.
  Cachegrind is not used: it crashes against this codebase (see
  `../renderer-cpu-profile/results.md:152`).

### The instrument had to be fixed first

The first repeatability run failed its own gate: `load_manifest_chain_z8_ms` and
`manifest_write_read_chain_z8_ms` moved more than 30% between two identical
runs. Those scenarios take a few hundred nanoseconds, close enough to the
clock's granularity that a median over single calls is noise. The bench now runs
a batch inside each timed region (batch size falling with zone count) and records
per-iteration time. After that change two consecutive runs compare clean with no
regressions, which is the gate the numbers below are entitled to.

Recorded artifacts: `topology.json` / `topology.csv` (113 metrics, post-fix),
`perf-report-demand-hub-z512.txt`.

## Scaling: the load path

Milliseconds, `profile` build, median. Ratio is the 128 → 512 growth for a 4x
increase in zone count; 4x would be linear, 16x quadratic.

| stage | chain z8 | z32 | z128 | z512 | 128→512 |
|---|---|---|---|---|---|
| `index_build` | 0.00023 | 0.00068 | 0.00486 | 0.04120 | 8.5x |
| `validate` | 0.00080 | 0.00453 | 0.03757 | 0.37868 | 10.1x |
| `load_manifest` (whole accept path) | 0.00202 | 0.00896 | 0.05653 | 0.46832 | 8.3x |
| `manifest_write_read` (JSON codec) | 0.03107 | 0.11721 | 0.47174 | 1.88186 | 4.0x |

`WorldPartitionIndex::Build` really is quadratic in construction —
`WorldPartitionIndex.cpp:63-74` walks the sorted zone id list and linear-scans
`manifest.Zones` for each one — and validation compounds it with its own
per-endpoint zone scans and a reachability BFS.

**It does not matter.** The entire runtime accept path for a 512-zone world is
under 0.5 ms, paid once at world load, against a JSON parse of the same manifest
that costs four times more. Extrapolating the observed growth, a 2048-zone world
would spend roughly 5 ms in validation — still once, still smaller than parsing.
No change is recommended. The recorded curve is the evidence for revisiting that
judgement if world sizes ever grow by another order of magnitude.

## Scaling: the per-frame demand path

This is the path `WorldPartitionRuntime::Update` runs every frame.

| stage | shape | z8 | z32 | z128 | z512 | 128→512 |
|---|---|---|---|---|---|---|
| `hop_ranks` | chain | 0.00016 | 0.00024 | 0.00060 | 0.00138 | 2.3x |
| | grid | 0.00027 | 0.00060 | 0.00168 | 0.00419 | 2.5x |
| | **hub** | 0.00012 | 0.00058 | **0.00585** | **0.06578** | **11.2x** |
| `demand` | chain | 0.00035 | 0.00049 | 0.00102 | 0.00229 | 2.3x |
| | **hub** | 0.00025 | 0.00076 | **0.00605** | **0.06690** | **11.1x** |

Chain, grid, and multi-graph are all comfortably sublinear and negligible in
absolute terms. The hub — one zone connected to every other, the shape a town or
a transit chamber takes — was 14x more expensive than a grid of the same size and
growing faster than the world.

### Why, and what the counters said

`perf record` on `demand_hub_z512` put **86.55%** of samples inside the
`consider` lambda in `ComputeZoneHopRanks`, split almost evenly between two
inlined helpers: `ZoneExists` ~43%, `GraphOf` ~42%. Both are full linear scans of
`manifest.Zones` (`ZoneDemand.cpp:12-18`, `:28-34`).

The mechanism: `consider()` ran three whole-zone-list scans per edge —
`ZoneExists(destination)`, `GraphOf(current.Zone)`, `GraphOf(destination)` —
before it reached the test that would reject the edge for exceeding the hop
budget. Expanding a 511-edge hub therefore cost about 785,000 header comparisons
to conclude that the budget was already spent. `GraphOf(current.Zone)` is the
same value for every edge leaving a frontier node, so two thirds of that was work
the loop had already done.

Counters for the same scenario, and they are the reason the fix is what it is:

| counter | value | derived |
|---|---|---|
| instructions | 2,206,232,968 | **3.72 IPC** |
| cycles | 592,774,763 | |
| L1-dcache-loads | 448,572,741 | **0.45% L1 miss** |
| L1-dcache-load-misses | 2,015,827 | |
| LLC-load-misses | 908,805 | 0.2% of loads reach LLC |
| branches | 862,886,712 | **0.19% branch miss** |
| branch-misses | 1,611,177 | |
| dTLB-load-misses | 12,247 | negligible |

This path was never memory-bound. At 3.72 instructions per cycle with a 0.45% L1
miss rate, the hardware was executing the wasteful algorithm about as fast as it
can be executed. There is nothing to win from data layout, prefetching, or a
tighter struct; the only thing available is doing less.

### The fix, and what it bought

`ZoneDemand.cpp`: hoist `GraphOf(current.Zone)` out of the per-edge lambda (it is
loop-invariant across every edge of a frontier node), and collapse
`ZoneExists(destination)` plus `GraphOf(destination)` into one `FindZoneHeader`
lookup that answers both. Three scans per edge become one, plus one per frontier
node. No behavior change: `FindZoneHeader` returning null is exactly the old
`ZoneExists` false, and the graph read is the same field the old `GraphOf`
returned.

Drift-normalized, same machine, same build:

| metric | before | after | change |
|---|---|---|---|
| `hop_ranks_hub_z512_ms` | 0.06578 | 0.03227 | **-47.8%** |
| `demand_hub_z512_ms` | 0.06690 | 0.03280 | **-47.9%** |
| `demand_radius_hub_z512_ms` | 0.06952 | 0.03503 | -46.4% |
| `hop_ranks_grid_z512_ms` | 0.00419 | 0.00195 | -50.7% |
| `hop_ranks_grid_z128_ms` | 0.00168 | 0.00066 | -58.1% |
| `demand_chain_z512_ms` | 0.00229 | 0.00165 | -23.4% |

Every shape improved; nothing regressed. The remaining hub cost is the one scan
per edge that a zone-id-to-graph lookup would remove entirely, which is worth
doing only if hub-shaped worlds get much larger — the curve to justify it is now
recorded.

## Lifecycle and edge cases

Eighteen new tests. What they found matters less than what they refute: two of
the three hazards this pass was written to confirm are not reachable.

### The detach window is real and unreachable

Between a detach being flushed and residency processing running, a zone reports
`IsZoneResident == false` while `FindZone` still returns a live record. Anything
deciding "may I load this zone" from residency alone would, in that window,
reissue a load for a zone that still exists — which is exactly what
`AsyncZoneLoader::BeginLoad` asserts against at `AsyncZoneLoader.cpp:69`, and
that assert compiles out of release builds.

The window exists;
`AsyncZoneLoaderLifetime.ADetachingZoneStopsBeingResidentBeforeItsRecordIsGone`
pins it. But the frame cannot observe it: `EngineFramePhases.cpp` runs
`FlushLifecycleRequests` at the end of `DrainAsyncTasks` (phase 3) and
`FinalizeResidencyProcessing` at the end of `ZoneResidency` (phase 4), while the
partition update that issues loads runs at phase 7. The window opens and closes
two phases before anything can look into it.
`FlushAndResidencyClosePairwiseSoNoUpdateSeesTheWindow` pins that pairing, so a
future phase reordering fails a test instead of producing a duplicate partition
in a shipping build.

**Status: latent, guarded by phase ordering, now covered. No fix needed.**

### Teardown with loads in flight does not reproduce

`AsyncZoneLoader`'s commit continuation captures `this`, the loader holds
references to the queue, the world, the schema, and the serializers, and it
declares no destructor — so whether teardown is safe rests on member declaration
order at each construction site. Four tests destroy a harness mid-flight: with a
build queued and never pumped, with a completed build whose commit was never
drained, with four worker threads racing destruction, and with the whole thing
rebuilt afterwards.

All clean, including under AddressSanitizer (a dedicated `build-asan-pass` build;
the full 373-test runtime suite reports zero sanitizer findings). The reason is
that `AsyncTaskQueue`'s destructor joins its workers and drops queued commits
rather than running them, so a captured `this` is destroyed, never dereferenced.

**Status: not reproduced. The safety is real but implicit** — it depends on the
queue outliving the loader at every construction site, and on the destructor
continuing to drop rather than drain. Both are now exercised.

The one path this pass could not test is the asset-gated variant, where the
continuation lives in an `AssetPreload` the caller may hold: `AssetPreload`'s
constructor is private to `AssetPreloader`, so reaching it needs the asset system
booted. `AssetPreloader`'s own header already states the contract ("the preloader
must outlive any preload it has in flight (same contract as AsyncZoneLoader)").
Worth a targeted test whenever that layer next gets attention.

### Demand flapping is damped, and the damper is linger

Focus resolution has no hysteresis: outside every zone AABB the nearest box wins
and the previous focus earns nothing
(`ZoneDemandStability.FocusResolutionOutsideEveryZoneIgnoresThePreviousZone`
records this). So a pawn jittering on a boundary does flip focus every frame.

That does not become churn. With the default three-second linger, 240 frames of
boundary jitter produce zero re-attaches once both neighbourhoods are resident.
An initial widening is legitimate and expected — standing in zone 2 asks for zone
3, which standing in zone 1 did not — and the test absorbs that in a warm-up and
bounds what happens after.

Also confirmed: a stationary focus reaches a fixed point and stops issuing builds
entirely; walking a 24-zone chain with a 4-zone cap keeps residency bounded; and
repeated identical serial runs produce a byte-identical *decision sequence*, not
merely the same end state.

**Status: healthy. No hysteresis needed today.** If linger is ever shortened
toward zero, the damper goes with it — the zero-linger churn number is recorded
by `SeamJitterWithoutLingerIsMeasuredNotAssumed` for that conversation.

### The cook-to-runtime seam is sound

The cook emits side B of a dock by negating normal and right and keeping up, and
validation independently requires exactly that. The two literals lived in
separate files with no test binding them. Five tests now drive authored
components through `CookWorldTopology`, real JSON serialization, parsing,
validation, `LoadManifest`, and a focus sweep that actually crosses the plane —
so the convention is asserted by consequence. Deliberately breaking the mirror,
or pointing an endpoint at a zone that does not exist, is rejected; this is also
the first direct coverage of `partition.dock.endpoint_invalid`, which the
validation suite only ever exercised through legacy transitions.

### Smaller findings

- **Partition slots are recycled by value.** A `StoragePartitionId` captured before a
  detach names whatever zone next takes the slot; there is no generation to detect it.
  `APartitionSlotIsReusedByValueAfterDetach` records the behavior. Callers rebuild
  partition sets from the frame view, so nothing is broken today, but the lease
  mechanism next door is generational and this is not.
- **The traversal fixture advanced the change epoch twice per frame.** It kept its own
  `AdvanceFrame` after `RuntimeWorld::EndFrameView` took over the job (commit
  `b5eeb861`). Removed. Expected to shift the traversal metrics; it did not — every
  counted metric in the streaming bench is byte-identical across the change, because
  transform propagation compares write frame against last-sweep frame and advancing by
  two per frame preserves that ordering. A correctness cleanup, not a measurement fix.
- **Demand output is bounded by neighbourhood, not world size**, at every scale and
  shape tested. Now a permanent bound
  (`ZoneTopologyScaling.DemandSetSizeDoesNotGrowWithWorldSize`).

## What was not done

- **No live renderer-attached measurement.** `TemplateGame` can stream a cooked world
  via `+world`, and its console commands are the right driver, but the template game
  module only builds under `SENCHA_ENABLE_VULKAN AND SENCHA_BUILD_TEMPLATE` (neither on
  in the `dev` preset), no `.sworld.json` exists anywhere in the repo, and authoring one
  goes through the Kyusu editor's `WorldCook`. Reaching that venue is a rebuild plus
  content authoring, which was out of proportion to what it would add here. Everything
  in this document is owner-thread cost; GPU-side streaming cost remains unmeasured,
  the same gap `unified-world-hardening.md` criterion 5 already records.
- **No allocation counting.** Planned via `operator new` interposition; the scaling
  curves answered the question the allocation counts were meant to inform, so the
  instrument was not built.
## Verification

- `ctest --preset dev`, serial: **1913 tests, 100% passed** (1889 before this pass; 24 added).
- AddressSanitizer (`build-asan-pass`, `-DSENCHA_ENABLE_ASAN=ON`): full runtime suite,
  373 tests, **zero sanitizer findings**, leak detection on.
- ThreadSanitizer (`tsan` preset): 27 streaming, traversal, async-load, and lifetime
  tests across worker counts, **no races reported**.
- `scripts/bench_streaming.sh` compared against the committed unified-world baseline
  after the epoch-advance change: no regressions, every count metric identical.
- The metric recorder was extracted to `test/runtime/BenchRecorder.h` so the two bench
  generators cannot drift apart in a format the compare script reads. Proven neutral:
  the streaming bench emits the same 51 metrics with every count identical across the
  extraction. One run flagged `traversal_worst_event_frame_ms` at +26% normalized; it is
  a single worst-frame sample and a repeat run put it at +1% with no regressions.
- `scripts/bench_topology.sh` run twice for repeatability before any number here was
  trusted, and again after the demand fix for the before/after table.
- `git diff --check`: clean.

## Reproducing

```sh
cmake --preset profile && cmake --build --preset profile --target runtime_tests --parallel
scripts/bench_topology.sh                      # writes build-profile/bench/topology.json
python3 scripts/bench_streaming_compare.py \
    docs/plans/evidence/zone-streaming-stress/topology.json build-profile/bench/topology.json

PC=$(cat /sys/devices/cpu_core/cpus)
SENCHA_TOPOLOGY_BENCH_OUT=/tmp/perf.json SENCHA_TOPOLOGY_BENCH_ONLY=demand_hub_z512 \
  taskset -c "$PC" perf stat -r 3 \
  -e cycles:u,instructions:u,L1-dcache-loads:u,L1-dcache-load-misses:u \
  ./build-profile/test/runtime_tests --gtest_filter='TopologyBench.Generate'

./build/test/runtime_tests --gtest_filter='ZoneTopologyScaling*:AsyncZoneLoaderLifetime*:ZoneDemandStability*:WorldTopologyCookSeam*'
```
