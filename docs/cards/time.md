# time

Snapshot `97ed48e4` (2026-10-30). Layer: foundation.

## 1. Frame phases

- `ScheduleTicks`: the runtime frame loop's `FixedStepScheduler` (`engine/include/runtime/RuntimeFrameLoop.h:158`) sets the tick budget used by `ScheduleTicks` (`engine/src/app/EngineFramePhases.cpp:754`).
- `Simulate`: `RunFixedLogic` (`engine/src/app/EngineFramePhases.cpp:791`) runs `ActorTimeSystem` (`engine/src/abilities/AbilityKit.cpp:67`).

## 2. Inputs

- Wall-clock seconds per frame and the fixed step, through `FixedStepScheduler::Advance` (`engine/include/time/FixedStepScheduler.h:47`).
- Time-scale claims, `ActorTimeLease` (`engine/include/time/ActorTimeLeases.h:23`), taken through `AcquireActorTimeLease` (`engine/include/time/ActorTimeLeases.h:59`) by abilities and effects.
- Module dependencies: app, authored, core, ecs, net, profiling, world.

## 3. Outputs

- `FixedStepPlan` (`engine/include/time/FixedStepScheduler.h:26`): how many ticks run this frame, consumed by the runtime frame loop.
- `ActorTime` (`engine/include/time/ActorTime.h:13`) and `ActorTimeDilated` (`engine/include/time/ActorTime.h:25`): each actor's local clock, consumed by movement, abilities, animation and effects.
- `ActorTimeLeasesWire` (`engine/include/time/ActorTimeLeasesWire.h:27`): replicated lease state, consumed by remote machines.

## 4. Central decisions

- `FixedStepScheduler::Advance` (`engine/include/time/FixedStepScheduler.h:47`): turns elapsed time into whole ticks, keeping the residual and capping a backlog.
- `AdvanceActorTime` (`engine/include/time/ActorTimeSystem.h:39`): sets each actor's scale from the leases in force this tick.
- `FrameCadenceLock::Apply` (`engine/src/time/FrameCadenceLock.cpp:11`): decides when a frame's elapsed time snaps to the display cadence.

## 5. Invariants

- Elapsed time is conserved across irregular frames: `FixedStepScheduler.ConservesInputAcrossIrregularFrames` (`test/core/FixedStepSchedulerTests.cpp`).
- A backlog cap keeps the residual under one tick: `FixedStepScheduler.CapDropsBacklogAndKeepsResidualBelowOneTick` (`test/core/FixedStepSchedulerTests.cpp`).
- Identical scripted clocks produce identical tick streams: `RuntimeFrameLoop.IdenticalScriptedClocksProduceIdenticalTickStreams` (`test/core/TimeServiceTests.cpp`).
- An actor's scale does not depend on lease order: `ActorTime.TheScaleIsIdenticalWhateverOrderTheLeasesWereTakenIn` (`test/runtime/ActorTimeTests.cpp`).
- Dilation slows an actor's clock and never the tick cadence: `ActorTime.DilationSlowsTheClockAndNeverTheCadence` (`test/runtime/ActorTimeTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 1,309 lines in 18 files.
- CCN 14 `ActorTimeOperations::Scale` (`engine/src/time/ActorTimeVerbs.cpp:32`); CCN 12 `FrameCadenceLock::Apply` (`engine/src/time/FrameCadenceLock.cpp:11`); CCN 8 `FixedStepScheduler::Advance` (`engine/include/time/FixedStepScheduler.h:47`).

## 7. Docs that contradict the code

- None found.
