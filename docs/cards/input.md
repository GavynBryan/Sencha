# input

Snapshot `97ed48e4` (2026-10-30). Layer: motion and space.

## 1. Frame phases

- `ScheduleTicks` (PreSimulate lane): `InputActionResolveSystem::PreSimulate` (`engine/include/input/InputActionResolveSystem.h:46`) attributes this frame's device input to the ticks about to run.
- `Simulate` (FixedLogic lane): `InputActionResolveSystem::FixedLogic` (`engine/include/input/InputActionResolveSystem.h:47`) resolves actions per tick.
- `RegisterInputSystems` (`engine/src/app/Engine.cpp:1387`) is the engine's call site.

## 2. Inputs

- Device events from SDL, captured by `SdlInputCapture` (`engine/include/input/SdlInputCapture.h:21`) and `SdlGamepadCapture` (`engine/include/input/SdlGamepadCapture.h:27`) into `InputFrame` (`engine/include/input/InputFrame.h:80`).
- Authored action sets and profiles, `CompiledInputProfile` (`engine/include/input/InputProfileData.h:52`).
- Active contexts, `InputContextSet` (`engine/include/input/InputContextSet.h:40`).
- Module dependencies: app, assets, core, ecs, math, platform, runtime, world.

## 3. Outputs

- `InputActionState` (`engine/include/input/InputActionState.h:78`): per-tick action values and edges, consumed by controller, abilities, UI and net.
- `InputActionRegistry` (`engine/include/input/InputActionRegistry.h:32`): dense action ids, consumed by every binding consumer.
- `InputActionSources` (`engine/include/input/InputActionSource.h:120`): which local player drives which entity, consumed by abilities.

## 4. Central decisions

- `ResolveInputActions` (`engine/src/input/InputActionResolve.cpp:608`): turns one tick's device snapshot into action values, edges and claims.
- `EvaluateBinding` (`engine/src/input/InputActionResolve.cpp:134`): computes one binding's contribution, with dead zones, scale and composites.
- `CompileInputActionSet` (`engine/src/input/InputProfileData.cpp:362`): accepts or refuses an authored action set.

## 5. Invariants

- A tap inside one frame keeps both edges: `InputResolve.TapInsideOneFrameKeepsBothEdges` (`test/framework/InputActionResolveTests.cpp`).
- Catch-up ticks do not repeat the press edge: `InputResolve.CatchUpTicksDoNotRepeatThePressEdge` (`test/framework/InputActionResolveTests.cpp`).
- Splitting motion conserves the total: `InputResolve.SplittingMotionConservesTheTotal` (`test/framework/InputActionResolveTests.cpp`).
- A higher-priority context claims the control: `InputResolve.HigherPriorityContextClaimsTheControl` (`test/framework/InputActionResolveTests.cpp`).
- A dropped name retires its slot rather than lending it: `InputActionRegistryTests.ADroppedNameRetiresItsSlotRatherThanLendingIt` (`test/framework/InputActionResolveTests.cpp`).
- A rejected rebuild leaves the previous vocabulary intact: `InputActionRegistryTests.ARejectedRebuildLeavesThePreviousVocabularyIntact` (`test/framework/InputActionResolveTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 5,435 lines in 42 files.
- CCN 25 `CompileInputActionSet` (`engine/src/input/InputProfileData.cpp:362`); CCN 23 `ResolveInputActions` (`engine/src/input/InputActionResolve.cpp:608`); CCN 21 `EvaluateBinding` (`engine/src/input/InputActionResolve.cpp:134`).

## 7. Docs that contradict the code

- None found.
