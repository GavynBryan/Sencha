# ecs

Snapshot `97ed48e4` (2026-10-30). Layer: foundation.

## 1. Frame phases

- None of its own: a library every phase reads and writes; structural changes recorded during iteration apply at `CommandBuffer::Flush` (`engine/include/ecs/CommandBuffer.h:165`).

## 2. Inputs

- Component types and their declared stable names, through `World::RegisterComponent` (`engine/include/ecs/World.h:122`) and `MakeComponentTypeId` (`engine/include/ecs/ComponentTypeId.h:30`).
- A frozen registration recipe, `WorldComponentSchema` (`engine/include/ecs/WorldComponentSchema.h:26`), applied by `WorldComponentSchema::Apply` (`engine/include/ecs/WorldComponentSchema.h:206`).
- Lifecycle hooks from `ComponentTraits<T>` (`engine/include/ecs/ComponentTraits.h:31`).
- Module dependencies: core only.

## 3. Outputs

- Entity storage, `World` (`engine/include/ecs/World.h:75`): read by 32 of the 40 engine modules.
- Chunk iteration, `Query` (`engine/include/ecs/Query.h:112`) handing out `ChunkView` (`engine/include/ecs/Query.h:41`): consumed by every scheduled system.
- Structural versions, `World::StructuralVersion` (`engine/include/ecs/World.h:753`): consumed by navigation, physics and world to skip unchanged work.
- Partition-move facts, `EntityPartitionMove` (`engine/include/ecs/World.h:62`), delivered by `DispatchPartitionMoves` (`engine/include/ecs/PartitionFollowers.h:47`) to followers in `engine/src/world/RuntimeWorld.cpp` and `engine/src/attach/Attachment.cpp`.

## 4. Central decisions

- `World::RegisterComponent` (`engine/include/ecs/World.h:122`): assigns a `ComponentId`, or returns the existing one for an already-registered stable name.
- `World::GetOrCreateArchetype` (`engine/include/ecs/World.h:1415`): finds or builds the archetype for a component signature.
- `Archetype::RemoveRow` (`engine/include/ecs/Archetype.h:180`): swap-removes a row within its chunk and reports which entity's row moved.
- `CommandBuffer::Flush` (`engine/src/ecs/CommandBuffer.cpp:7`): applies recorded creations, additions, removals and destructions in record order.
- `Query::ForEachChunk` (`engine/include/ecs/Query.h:127`): selects matching archetypes and chunks, including `Changed<T>` filtering against a reference frame.

## 5. Invariants

- A recycled entity index carries a new generation: `EcsTest.RecycledIndexHasDifferentGeneration` (`test/ecs/EcsTests.cpp`).
- A swap-remove updates the moved entity's location: `EcsTest.SwapAndPopUpdatesMovedEntityLocation` (`test/ecs/EcsTests.cpp`).
- Component data survives an archetype transition: `EcsTest.DataPreservedAcrossArchetypeTransition` (`test/ecs/EcsTests.cpp`).
- Flush order equals record order: `EcsTest.Invariant_CommandBufferFlushOrderMatchesRecordOrder` (`test/ecs/EcsTests.cpp`).
- Change detection is per chunk and write access counts as a write: `EcsTest.Changed_IsTrackedPerChunk`, `EcsTest.Invariant_WriteAccessBumpsColumnVersionConservatively` (`test/ecs/EcsTests.cpp`).
- One schema yields the same ids in independent worlds: `WorldComponentSchema.SameSchemaProducesSameIdsInIndependentWorlds` (`test/ecs/WorldComponentSchemaTests.cpp`).
- Partition migration keeps identity and values: `WorldStoragePartitionTest.MigrationPreservesIdentityAndComponentValues` (`test/ecs/WorldStoragePartitionTests.cpp`).

### Unchecked

- In Release builds, stable-name collisions with a different layout, registration after the first entity, and structural changes during a query: the death tests skip under `NDEBUG` (`test/ecs/EcsTests.cpp:106`) or compile out (`test/ecs/WorldComponentSchemaTests.cpp:318`).
- Resource teardown order: `ClearOwnedTypeErased` destroys resources in hash-table order (`engine/include/ecs/World.h:1236`), and no test pins an order.

## 6. Size

- 3,864 lines in 23 files.
- CCN 35 `CommandBuffer::Flush` (`engine/src/ecs/CommandBuffer.cpp:7`); CCN 23 `WorldComponentSchema::Add` (`engine/include/ecs/WorldComponentSchema.h:74`); CCN 16 `World::RegisterComponent` (`engine/include/ecs/World.h:122`).

## 7. Docs that contradict the code

- `docs/ecs/world-component-schema.md:82` calls `EngineSceneComponents` the authoritative component list; no such symbol exists in the tree.
- `docs/ecs/parallelization.md:211` names `ZoneRuntime` as a current type; no such type exists in the tree.
- `docs/ecs/component-traits.md:172` names `AudioComponent`; no such type exists in the tree.
