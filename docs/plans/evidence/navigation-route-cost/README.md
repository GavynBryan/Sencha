# Navigation route cost

Evidence for `docs/plans/navigation-agent.md`, section 9 (decisions D2 and D11).

## Method

1. Copy `NavRouteCostProbe.cpp` into `test/navigation/`, where the
   `navigation_tests` glob picks it up.
2. Configure and build with the `profile` preset (Release `-O3`, frame
   pointers):

   ```sh
   cmake --preset profile
   cmake --build build-profile --target navigation_tests
   ```
3. Run it pinned to one core:

   ```sh
   taskset -c 3 build-profile/test/navigation_tests --gtest_filter='NavRouteCostProbe.*'
   ```

Conditions:

- **Build profile:** the templates' own `navigation.sdata`: radius 0.3, height
  1.8, 0.15 m cells, 32-cell tiles.
- **Scenes:**
  - square floors with 1 m pillars every 4 m;
  - "maze" scenes, which add walls every 12 m with alternating 4 m gaps.
- **Samples:** 512 random point pairs per scene, from a fixed seed. Endpoints
  are projected first, and 16 warm-up queries run before timing.
- **Context:** one `NavQueryContext` with the stated node budget and a
  512-polygon corridor.
- **Route buffer:** `NavRouteBuffer(32, 256, 256)`.

Recorded 2026-10-15 on `origin/main` `8f13cb5e`, Fedora 43, 16 hardware
threads, on one pinned core.

## Results

| Scene | Nodes (cap) | FindRoute median / p90 / max µs | ok / limit / other | max nodes | max corners / tile deps | Validate median / max µs |
| --- | --- | --- | --- | --- | --- | --- |
| 48 m pillars | 2048 | 6.4 / 16.8 / 28.9 | 512 / 0 / 0 | 259 | 11 / 17 | 0.034 / 0.437 |
| 96 m pillars | 2048 | 19.0 / 58.6 / 105.2 | 512 / 0 / 0 | 898 | 21 / 35 | 0.065 / 0.375 |
| 96 m pillars, ends ≥ 60 m apart | 2048 | 45.0 / 76.6 / 119.4 | 512 / 0 / 0 | 914 | 21 / 36 | 0.095 / 0.632 |
| 96 m pillars, ends ≥ 60 m apart | 8192 | 45.2 / 78.5 / 228.7 | 512 / 0 / 0 | 914 | 21 / 36 | 0.095 / 0.298 |
| 96 m maze | 2048 | 82.6 / 155.5 / 261.4 | 512 / 0 / 0 | 1841 | 54 / 96 | 0.158 / 0.697 |
| 96 m maze | 8192 | 83.1 / 154.4 / 225.0 | 512 / 0 / 0 | 1841 | 54 / 96 | 0.161 / 0.720 |
| 192 m maze | 2048 | 181.8 / 203.3 / 312.5 | 143 / 369 / 0 | 2179 | 28 / 95 | 0.118 / 0.509 |
| 192 m maze | 16384 | 354.3 / 611.7 / 798.8 | 273 / 0 / 239 | 7554 | 41 / 133 | 0.249 / 0.799 |

How to read the columns:

- "limit" counts `SearchLimitReached`.
- "other" in the last row counts `OutputCapacityReached`. Those routes overflowed
  the 512-polygon corridor; the route buffer's dependency capacity was 256 and
  was not the limit.
- The default `NavRouteBuffer` holds 64 tile dependencies, fewer than the 96 the
  96 m maze needed.

Other operations, median / p90:

- `ProjectPoint`: 0.50–0.60 / 0.85–1.12 µs.
- `NavRaycast`, over 10% of the pair distance: 0.23–0.52 / 0.40–0.80 µs.

# Planning batches and admission policies

`NavPlanningBatchProbe.cpp` measures the planning shape D11 proposes:

- **Gather:** inputs are positions only.
- **Plan:** each job projects both ends and calls `NavFindRoute`, with one
  `NavQueryContext` per `JobSystem` worker index (4096 nodes, 512 corridor) and
  one output route buffer per request (`NavRouteBuffer(32, 64, 128)`).
- **Commit:** none needed for the measurement.

Batches of fewer than 2 plans run inline on the caller.

Run it the same way as the route-cost probe, unpinned, so workers have cores.
Machine load average was about 2 during the run. Recorded 2026-10-15, same
build and machine as above (16 hardware threads, so
`JobSystem::DefaultWorkerCount()` = 14).

Workloads are 256 requests each, on the 96 m scenes above:

- "random" uses seeded random pairs;
- "converging" sends every agent to one point, the shape of a repath storm after
  a shared door or bridge changes.

## Dispatch overhead

An empty `ParallelFor` of `workers + 1` jobs, median of 2000:

| Workers | Overhead |
| --- | --- |
| 1 | 0.08 µs |
| 3 | 0.12 µs |
| 7 | 1.6 µs |
| 14 | 4.0 µs |

One plan costs 25–170 µs.

## Batch wall time

Each cell is one batch of n plans, median, in µs.

| Workload | Workers | n=1 | 2 | 4 | 8 | 16 | 32 | 64 | 256 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| pillars random | 0 | 22 | 49 | 92 | 198 | 426 | 809 | 1569 | 6518 |
| pillars random | 3 | 35 | 60 | 85 | 125 | 211 | 375 | 666 | 2152 |
| pillars random | 7 | 27 | 59 | 73 | 133 | 159 | 256 | 437 | 1460 |
| pillars random | 14 | 28 | 69 | 73 | 103 | 146 | 200 | 298 | 1027 |
| maze random | 0 | 87 | 199 | 319 | 627 | 1213 | 2439 | 4620 | 19634 |
| maze random | 3 | 69 | 144 | 217 | 277 | 496 | 892 | 1765 | 6170 |
| maze random | 7 | 77 | 157 | 179 | 240 | 357 | 623 | 1112 | 4035 |
| maze random | 14 | 122 | 156 | 176 | 236 | 292 | 466 | 760 | 3028 |
| maze converging | 0 | 167 | 262 | 403 | 808 | 1654 | 3315 | 6536 | 26421 |
| maze converging | 3 | 136 | 170 | 212 | 348 | 713 | 1316 | 2342 | 10164 |
| maze converging | 7 | 176 | 270 | 243 | 306 | 459 | 823 | 1444 | 5869 |
| maze converging | 14 | 181 | 213 | 245 | 295 | 407 | 787 | 1360 | 5113 |

The run also covered 1 worker; it is in `batch-raw.txt`.

Every worker count produced output identical to the serial run: status, nodes
visited, and corners compared bit for bit.

The n=1 column always runs inline. Its spread across rows is run-to-run noise,
not dispatch cost.

## Draining a 256-request storm

Every request is enqueued at tick 0, and each tick admits plans under the
policy.

- Each figure is the median of 5 drains.
- "count N" admits N plans per tick as one batch.
- "nodes B, waves W, cap C" admits waves of W plans until the tick's visited
  nodes reach B or C plans have run. Node counts are known only after a wave, so
  a tick overshoots by up to one wave.
- No policy's admission depends on the worker count, so serial and parallel
  runs plan the same requests on the same ticks.

| Workload | Policy | Ticks | Tick median / max, 0 workers (µs) | 3 workers | 7 workers | 14 workers | Max nodes in a tick |
| --- | --- | --- | --- | --- | --- | --- | --- |
| maze random | count 4 | 64 | 313 / 577 | 170 / 287 | 164 / 294 | 180 / 638 | 5998 |
| maze random | count 16 | 16 | 1247 / 1595 | 446 / 645 | 342 / 460 | 309 / 415 | 16733 |
| maze random | count 32 | 8 | 2771 / 3219 | 918 / 1153 | 610 / 715 | 471 / 676 | 32540 |
| maze random | count 64 | 4 | 5257 / 5469 | 1680 / 1868 | 1009 / 1069 | 792 / 815 | 57147 |
| maze random | nodes 8k, waves 8, cap 64 | 17 | 1164 / 1500 | 503 / 684 | 406 / 563 | 418 / 575 | 15807 |
| maze random | nodes 16k, waves 8, cap 64 | 11 | 1997 / 2536 | 878 / 1120 | 725 / 1052 | 661 / 896 | 23363 |
| maze random | nodes 32k, waves 16, cap 128 | 5 | 3997 / 4605 | 1369 / 1670 | 1092 / 1274 | 1012 / 1224 | 46483 |
| maze converging | count 4 | 64 | 513 / 740 | 241 / 463 | 233 / 410 | 222 / 342 | 6862 |
| maze converging | count 16 | 16 | 1693 / 2049 | 648 / 948 | 450 / 606 | 359 / 713 | 21997 |
| maze converging | count 32 | 8 | 3349 / 3999 | 1082 / 1268 | 774 / 909 | 562 / 880 | 43065 |
| maze converging | count 64 | 4 | 6444 / 7663 | 2416 / 2788 | 1509 / 1739 | 1127 / 1217 | 83185 |
| maze converging | nodes 8k, waves 8, cap 64 | 25 | 1041 / 2037 | 401 / 890 | 337 / 709 | 289 / 593 | 18277 |
| maze converging | nodes 16k, waves 8, cap 64 | 15 | 1706 / 2462 | 710 / 1075 | 562 / 890 | 569 / 1171 | 26633 |
| maze converging | nodes 32k, waves 16, cap 128 | 7 | 3703 / 4874 | 1303 / 1945 | 976 / 1324 | 667 / 1016 | 52034 |
| pillars random | count 4 | 64 | 99 / 283 | 66 / 182 | 55 / 125 | 60 / 182 | 2251 |
| pillars random | count 16 | 16 | 422 / 579 | 161 / 281 | 133 / 207 | 122 / 205 | 4592 |
| pillars random | count 32 | 8 | 905 / 1158 | 325 / 420 | 188 / 229 | 167 / 236 | 8452 |
| pillars random | count 64 | 4 | 1771 / 1949 | 594 / 638 | 316 / 354 | 269 / 315 | 13929 |
| pillars random | nodes 8k, waves 8, cap 64 | 6 | 1152 / 1316 | 702 / 801 | 528 / 634 | 508 / 610 | 10249 |
| pillars random | nodes 16k, waves 8, cap 64 | 4 | 1685 / 1786 | 895 / 1031 | 756 / 806 | 756 / 769 | 13929 |
| pillars random | nodes 32k, waves 16, cap 128 | 2 | 3477 / 3477 | 1449 / 1449 | 1117 / 1117 | 1014 / 1014 | 27092 |

## Context memory

Building 15 `NavQueryContext`s plus the planner's own, warming each with one
maze query, and allocating 15 route buffers grew resident memory as follows:

| Context size | Resident growth |
| --- | --- |
| 2048 nodes | 2.7 MB |
| 4096 nodes | 4.3 MB |

That is about 0.17 MB and 0.26 MB per warmed context respectively.
