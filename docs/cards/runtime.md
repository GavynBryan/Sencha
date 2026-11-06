# runtime

Snapshot `97ed48e4` (2026-10-30). Layer: world and frame.

## 1. Frame phases

- All of them: `FrameDriver::StepOnce` (`engine/src/runtime/FrameDriver.cpp:63`) runs the callbacks registered for each `FramePhase` (`engine/include/runtime/FrameDriver.h:17`) in enum order.
- `ScheduleTicks`: `RuntimeFrameLoop::BeginFrame` (`engine/include/runtime/RuntimeFrameLoop.h:91`) produces the tick budget that `ScheduleFixedTicks` (`engine/src/app/EngineFramePhases.cpp:757`) turns into due ticks.
- `EndFrame`: `FramePacer::Wait` (`engine/include/runtime/FramePacer.h:28`) applies the frame-rate limit.

## 2. Inputs

- Wall-clock time, window lifecycle events and resize state, read by `RuntimeFrameLoop` (`engine/include/runtime/RuntimeFrameLoop.h:88`).
- Phase callbacks, registered with `FrameDriver` (`engine/include/runtime/FrameDriver.h:61`) by app.
- Spawn requests, through `SceneSpawnService::RequestSpawn` (`engine/include/runtime/spawn/SceneSpawnService.h:93`).
- Module dependencies: assets, core, ecs, input, jobs, math, net, platform, time, world, zone.

## 3. Outputs

- `RuntimeFrameSnapshot` (`engine/include/runtime/RuntimeFrameLoop.h:68`): tick budget, lifecycle state and frame events, read by every phase through `PhaseContext` (`engine/include/runtime/FrameDriver.h:46`).
- `FrameDiscontinuityBus` (`engine/include/runtime/FrameDiscontinuityBus.h:36`): tells listeners when time jumped, consumed by transform history and interpolation.
- Spawned scene instances from `SceneSpawnService` (`engine/include/runtime/spawn/SceneSpawnService.h:68`) and networked prefabs from `NetPrefabSpawner` (`engine/include/runtime/spawn/NetPrefabSpawner.h:39`).

## 4. Central decisions

- `FrameDriver::StepOnce` (`engine/src/runtime/FrameDriver.cpp:63`): runs one frame, choosing between a full frame and a lifecycle-only frame.
- `RuntimeFrameLoop::BeginFrame` (`engine/include/runtime/RuntimeFrameLoop.h:91`): decides whether this frame simulates, how many ticks are due, and whether time was discontinuous.
- `FramePacer::Wait` (`engine/include/runtime/FramePacer.h:28`): sleeps then spins to the target frame time, or returns at once when pacing is off.

## 5. Invariants

- A burst of resizes emits zero ticks during lifecycle frames: `RuntimeFrameLoopScenario.BurstResizeEmitsZeroTicksDuringLifecycleFrames` (`test/runtime/FrameLoopScenarioTests.cpp`).
- A long minimize emits zero ticks: `RuntimeFrameLoopScenario.LongMinimizeEmitsZeroTicks` (`test/runtime/FrameLoopScenarioTests.cpp`).
- A lifecycle-only frame emits zero ticks: `RuntimeFrameLoop.LifecycleOnlyFrameEmitsZeroTicks` (`test/core/TimeServiceTests.cpp`).
- Async commits drain on the frame: `HeadlessFrameLoop.DrainsAsyncCommitsOnTheFrame` (`test/runtime/HeadlessFrameLoopTests.cpp`).
- Fixed ticks step with no graphics present: `HeadlessFrameLoop.StepsFixedTicksWithNoGraphics` (`test/runtime/HeadlessFrameLoopTests.cpp`).

### Unchecked

- `SwapchainRebuildWorker` (`engine/include/runtime/SwapchainRebuildWorker.h:24`) owns a thread outside the two lanes and is used only by tests; nothing checks it stays unused or is removed.

## 6. Size

- 2,075 lines in 15 files.
- CCN 16 `ToString` (`engine/src/runtime/FrameDriver.cpp:3`); CCN 16 `FrameDriver::StepOnce` (`engine/src/runtime/FrameDriver.cpp:63`); CCN 14 `SwapchainRebuildWorker::Poll` (`engine/src/runtime/SwapchainRebuildWorker.cpp:54`).

## 7. Docs that contradict the code

- None found.
