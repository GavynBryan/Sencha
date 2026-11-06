# movement

Snapshot `97ed48e4` (2026-10-30). Layer: motion and space.

## 1. Frame phases

- `Simulate` (FixedLogic lane): `RunFixedLogic` (`engine/src/app/EngineFramePhases.cpp:791`) runs ten systems, registered in order from `SupportTagProjectionSystem` (`engine/src/movement/MovementRegistration.cpp:108`) to `DilatedCharacterStepSystem` (`engine/src/movement/MovementRegistration.cpp:117`).
- `RegisterMovementSystems` (`templates/fps/src/FpsGame.cpp:98`) is called by each game template; the engine does not schedule movement itself.

## 2. Inputs

- What the controller wants, `MovementIntent` (`engine/include/movement/MovementIntent.h:25`), and mode requests, `ModeTransitionRequest` (`engine/include/movement/components/CharacterMovement.h:61`).
- Authored tuning layers, `CompiledMovementProfile` (`engine/include/movement/MovementProfileData.h:70`), resolved against gameplay tags.
- Root motion from animation, `RootMotionSource` (`engine/include/movement/RootMotionSource.h:29`).
- Module dependencies: abilities, anim, app, assets, attributes, authored, core, ecs, effects, gameplay_tags, math, physics, time, world.

## 3. Outputs

- `LocomotionOutput` (`engine/include/movement/components/MotionChannels.h:17`): the composed motion for the tick, consumed by the physics character controller.
- `ResolvedMovementTuning`, written by `MovementTuningResolutionSystem` (`engine/include/movement/MovementTuningResolutionSystem.h:9`): consumed by locomotion and jump.
- Support and mode tags, written by `SupportTagProjectionSystem` (`engine/include/movement/MovementModeSystems.h:46`): consumed by animation facts and ability conditions.
- Movement verbs, `MovementOperations` (`engine/include/movement/MovementVerbs.h:28`): consumed by abilities and responses.

## 4. Central decisions

- `LocomotionModeTransitionSystem::StepImpl` (`engine/src/movement/MovementModeSystems.cpp:70`): decides whether a requested mode replaces the current one this tick.
- `MovementTuningResolutionSystem::StepImpl` (`engine/src/movement/MovementTuningResolutionSystem.cpp:41`): folds the profile layers whose conditions hold into resolved tuning.
- `ComposeMotion` (`engine/include/movement/MotionComposition.h:25`): combines relative channels, support velocity, overrides and impulses into one output.

## 5. Invariants

- A higher request class replaces and the first write wins within a class: `MovementModeMailboxTest.HigherClassReplacesAndSameClassIsFirstWriteWins` (`test/framework/MovementArchitectureTests.cpp`).
- A candidate mode becomes the session atomically: `MovementModeTransitionTest.CandidateBecomesSessionAtomically` (`test/framework/MovementArchitectureTests.cpp`).
- Transient contributions are consumed once: `MotionCompositionTest.SystemConsumesTransientContributionsOnce` (`test/framework/MovementArchitectureTests.cpp`).
- Gravity integrates only when unsupported: `FreeLocomotionTest.GravityIntegratesOnlyWhenUnsupported` (`test/framework/MovementArchitectureTests.cpp`).
- Profile binding rejects unknown names: `MovementProfileData.BindingRejectsUnknownNames` (`test/framework/MovementProfileDataTests.cpp`).
- Replay reaches exactly where the live ticks reached: `PawnStateReplay.ReachesExactlyWhereTheLiveTicksReached` (`test/net/PawnStateReplayTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 4,860 lines in 49 files.
- CCN 30 `LocomotionModeTransitionSystem::StepImpl` (`engine/src/movement/MovementModeSystems.cpp:70`); CCN 21 `LayerMatches` (`engine/src/movement/MovementProfileRuntime.cpp:108`); CCN 20 `MovementTuningResolutionSystem::StepImpl` (`engine/src/movement/MovementTuningResolutionSystem.cpp:41`).

## 7. Docs that contradict the code

- `docs/gameplay/movement.md:65` places the movement components in `movement/MovementComponents.h`; that file does not exist, and they live under `engine/include/movement/components`.
