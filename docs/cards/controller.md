# controller

Snapshot `97ed48e4` (2026-10-30). Layer: motion and space.

## 1. Frame phases

- `ScheduleTicks` (PreSimulate lane): `LookIntegrationSystem::PreSimulate` (`engine/include/controller/LookIntegrationSystem.h:30`) gathers this frame's look input.
- `Simulate` (FixedLogic lane): `LookIntegrationSystem::FixedLogic` (`engine/include/controller/LookIntegrationSystem.h:31`) applies look per tick, and `AimFacingSystem::FixedLogic` (`engine/include/controller/AimFacingSystem.h:29`) turns tagged bodies to their aim.
- `RegisterControllerSystems` (`templates/fps/src/FpsGame.cpp:100`) is called by game templates; the engine does not schedule it.

## 2. Inputs

- Look input actions, through `LookInputBinding` (`engine/include/controller/LookOrientation.h:148`) and `PendingLookInput` (`engine/include/controller/LookOrientation.h:196`).
- Sensitivity, `LookSensitivity` (`engine/include/controller/LookOrientation.h:166`).
- Module dependencies: app, ecs, input, math, world.

## 3. Outputs

- `LocalLookControl` (`engine/include/controller/LookOrientation.h:101`): the pawn's yaw and pitch, consumed by `AimOf` and by game code through `engine/include/participant/LocalControl.h`.
- `Aim` (`engine/include/controller/Aim.h:8`), read through `AimOf` (`engine/include/controller/Aim.h:17`): consumed by abilities, projectile, logic and net.
- Body yaw on tagged entities: consumed by transform propagation.

## 4. Central decisions

- `AimOf` (`engine/src/controller/Aim.cpp:9`): decides an entity's aim origin and direction from its look or transform.
- `LookIntegrationSystem::FixedLogic` (`engine/src/controller/LookIntegrationSystem.cpp:78`): decides how much accumulated look each tick consumes.
- `AimFacingSystem::FixedLogic` (`engine/src/controller/AimFacingSystem.cpp:8`): turns each tagged body to its aim's yaw.

## 5. Invariants

- Pitch does not tip the body: `AimFacing.PitchDoesNotTipTheBody` (`test/framework/AimFacingTests.cpp`).
- A body and its camera agree on where the aim points: `AimFacing.ABodyAndItsCameraAgreeOnWhereTheAimPoints` (`test/framework/AimFacingTests.cpp`).
- Only tagged bodies turn: `AimFacing.EveryTaggedBodyTurnsAndOnlyTaggedOnesDo` (`test/framework/AimFacingTests.cpp`).
- A body outside the active partitions is not turned: `AimFacing.ABodyOutsideTheActivePartitionsIsNotTurned` (`test/framework/AimFacingTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 557 lines in 11 files.
- CCN 12 `AimOf` (`engine/src/controller/Aim.cpp:9`); CCN 9 `LookIntegrationSystem::FixedLogic` (`engine/src/controller/LookIntegrationSystem.cpp:78`); CCN 5 `AimFacingSystem::FixedLogic` (`engine/src/controller/AimFacingSystem.cpp:8`).

## 7. Docs that contradict the code

- None found.
