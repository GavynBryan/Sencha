# zone

Snapshot `97ed48e4` (2026-10-30). Layer: world and frame.

## 1. Frame phases

- `ZoneResidency`: the phase at `engine/src/app/EngineFramePhases.cpp:727` updates zone streaming and then `WorldPartitionRuntime` (`engine/src/app/EngineFramePhases.cpp:732`).
- `DrainAsyncTasks`: `AsyncZoneLoader` (`engine/include/zone/AsyncZoneLoader.h:39`) builds zones on `AsyncTaskQueue` (`engine/include/zone/AsyncZoneLoader.h:47`), whose results commit in `DrainAsyncTasks` (`engine/src/app/EngineFramePhases.cpp:713`).

## 2. Inputs

- The cooked world manifest, read by `ReadWorldPartitionManifest` (`engine/src/zone/WorldPartitionManifest.cpp:294`).
- Focus, pins and neighbour policy per update, turned into demand by `ComputeZoneDemand` (`engine/include/zone/ZoneDemand.h:183`).
- Zone content packages, imported by `ImportZonePackageHidden` (`engine/include/zone/ZonePackageImporter.h:61`).
- Module dependencies: assets, core, ecs, jobs, math, runtime, world.

## 3. Outputs

- Zone residency and participation, `ZoneParticipation` (`engine/include/zone/ZoneParticipation.h:3`): consumed by `RuntimeWorld` to build each frame's partition view.
- Demand records, `ZoneDemandRecord` (`engine/include/zone/ZoneDemand.h:44`): read by Kyusu's demand view and diagnostics.
- Topology components `Dock`, `Gate` and `Link`, and `WorldPartitionIndex` adjacency (`engine/include/zone/WorldPartitionIndex.h:13`): consumed by navigation, logic and net.
- Remembered zone state, `ZoneStateStore` (`engine/include/zone/ZoneStateStore.h:33`): replayed when a zone streams back in.

## 4. Central decisions

- `WorldPartitionRuntime::Update` (`engine/src/zone/WorldPartitionRuntime.cpp:426`): decides which zones load, attach, change participation, detach and unload this frame.
- `ComputeZoneDemand` (`engine/include/zone/ZoneDemand.h:183`): ranks zones by focus, pins, hops and cost, and applies the cap.
- `ValidateWorldPartitionManifest` (`engine/src/zone/WorldPartitionValidation.cpp:87`): accepts or refuses a manifest's ids, endpoints and pairings.
- `ImportZonePackageHidden` (`engine/include/zone/ZonePackageImporter.h:61`): builds a zone into a hidden partition so publication can be atomic.

## 5. Invariants

- The cap evicts by hop, then derived cost, then id: `ZoneDemand.CapEvictsByHopThenDerivedCostThenId` (`test/runtime/ZoneDemandTests.cpp`).
- A failed finalize cancels without publication: `AsyncZoneLoad.FailedFinalizeCancelsWithoutPublication` (`test/runtime/AsyncZoneLoadTests.cpp`).
- A destroyed entity stays destroyed across a restream: `ZoneStateMemory.DestroyedEntityStaysDestroyedAcrossRestream` (`test/runtime/ZoneStateStoreTests.cpp`).
- A self-loop in the manifest is reported: `WorldPartitionValidation.SelfLoopFires` (`test/runtime/WorldPartitionValidationTests.cpp`).
- A swept dock crossing survives fast movement: `DockCrossing.SweptCrossingHandlesFastMovement` (`test/runtime/DockCrossingTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 5,700 lines in 28 files.
- CCN 105 `ValidateWorldPartitionManifest` (`engine/src/zone/WorldPartitionValidation.cpp:87`); CCN 85 `ReadWorldPartitionManifest` (`engine/src/zone/WorldPartitionManifest.cpp:294`); CCN 78 `WorldPartitionRuntime::Update` (`engine/src/zone/WorldPartitionRuntime.cpp:426`).

## 7. Docs that contradict the code

- `docs/core-systems-map.md:42` lists a `ZoneRuntime` header under zones; no such file or symbol exists in the tree.
- `docs/core-systems-map.md:21` describes zone streaming through `ZoneLoadPackage`; no such type exists in the tree.
