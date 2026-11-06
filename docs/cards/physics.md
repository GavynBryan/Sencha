# physics

Snapshot `97ed48e4` (2026-10-30). Layer: motion and space.

## 1. Frame phases

- `Simulate` (Physics lane): `RunPhysics` (`engine/src/app/EngineFramePhases.cpp:804`) runs `PhysicsStepSystem::Physics` (`engine/include/physics/PhysicsStepSystem.h:21`), `CharacterControllerSystem::Physics` (`engine/include/physics/CharacterControllerSystem.h:17`) and `SensorOverlapSystem::Physics` (`engine/include/physics/SensorOverlapSystem.h:11`).
- `ZoneResidency`: `PhysicsStepSystem::ZoneResidency` (`engine/include/physics/PhysicsStepSystem.h:20`) evicts and restores bodies as zones change.
- `RegisterPhysics` (`templates/fps/src/FpsGame.cpp:88`) is called by each game template; the engine does not schedule physics itself.

## 2. Inputs

- Bodies, `RigidBody` (`engine/include/physics/components/RigidBody.h:21`), and collision shapes, `CollisionShape` (`engine/include/physics/CollisionShape.h:30`).
- Character moves, driven through `CharacterMoverPool` (`engine/include/physics/CharacterMoverPool.h:23`) by movement's character step (`engine/src/movement/CharacterTickStep.cpp`).
- Zone collision geometry, `LoadZoneCollision` (`engine/include/physics/ZoneCollisionLoader.h:16`).
- Module dependencies: app, authored, core, ecs, math, movement, time, world.

## 3. Outputs

- Simulated transforms written back by `RigidBodyBinding` (`engine/include/physics/RigidBodyBinding.h:32`): consumed by transform propagation and render.
- `CharacterMoveResult` (`engine/include/physics/CharacterMover.h:84`): support and collision results, returned to the mover pool by `Move` (`engine/src/physics/CharacterMoverPool.cpp:237`).
- `PhysicsQueries` (`engine/include/physics/PhysicsQueries.h:49`): rays and sweeps, consumed by projectile, perception, spatial and logic.
- Sensor enter, stay and exit, `SensorOccurrence` (`engine/include/physics/SensorOverlaps.h:26`): consumed by logic responses.

## 4. Central decisions

- `RigidBodyBinding::SyncToPhysics` (`engine/src/physics/RigidBodyBinding.cpp:230`): pushes changed authored transforms and shapes into the simulation.
- `RigidBodyBinding::Reconcile` (`engine/src/physics/RigidBodyBinding.cpp:141`): creates and removes bodies when components or residency change.
- `SensorOverlaps::Commit` (`engine/src/physics/SensorOverlaps.cpp:40`): turns this tick's overlap sample into enter, stay and exit occurrences.

## 5. Invariants

- Sensor occurrences are sorted independent of observation order and generationally safe: `SensorOverlaps.SortedIndependentOfObservationOrderAndGenerationallySafe` (`test/physics/SensorOverlapTests.cpp`).
- Overflow rejects the whole sample without false exits: `SensorOverlaps.OverflowRejectsWholeSampleWithoutFalseExits` (`test/physics/SensorOverlapTests.cpp`).
- A fast character crossing is observed: `SensorPhysics.FastCharacterCrossingIsObserved` (`test/physics/SensorOverlapTests.cpp`).
- Reconcile restores evicted bodies faithfully: `PhysicsResidency.ReconcileRestoresEvictedBodiesFaithfully` (`test/physics/PhysicsResidencyTests.cpp`).
- Evict and restore cycles are stable: `PhysicsResidency.EvictRestoreCyclesAreStable` (`test/physics/PhysicsResidencyTests.cpp`).
- Reconcile skips when nothing structural changed: `RigidBodyBinding.ReconcileSkipsWhenNothingStructuralChanged` (`test/physics/RigidBodyBindingTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 3,751 lines in 42 files.
- CCN 17 `RigidBodyBinding::SyncToPhysics` (`engine/src/physics/RigidBodyBinding.cpp:230`); CCN 16 `RigidBodyBinding::Reconcile` (`engine/src/physics/RigidBodyBinding.cpp:141`); CCN 14 `SensorOverlaps::Commit` (`engine/src/physics/SensorOverlaps.cpp:40`).

## 7. Docs that contradict the code

- None found.
