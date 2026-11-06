# world

Snapshot `97ed48e4` (2026-10-30). Layer: world and frame.

## 1. Frame phases

- `ZoneResidency` onward: `RuntimeWorld` (`engine/include/world/RuntimeWorld.h:74`) builds each frame's `FrameZoneView` (`engine/include/world/RuntimeWorld.h:51`), which every later phase reads.
- `Simulate`: `PropagateTransforms` (`engine/src/app/EngineFramePhases.cpp:806`) and `CaptureWorldTransformHistory` (`engine/src/app/EngineFramePhases.cpp:824`) close each tick.
- `ExtractRender`: `PropagateTransforms` runs again for presentation at `engine/src/app/EngineFramePhases.cpp:1137`.

## 2. Inputs

- Scene files, JSON through `LoadSceneJson` (`engine/src/world/serialization/SceneSerializer.cpp:158`) and cooked `.smap` through `ParseContainer` (`engine/src/world/scene/SmapFormat.cpp:76`).
- Component vocabularies from every module, through `ComponentRegistrar` (`engine/include/world/ComponentRegistrar.h:108`).
- Module dependencies: 23 engine modules, because the engine's component vocabulary is assembled here.

## 3. Outputs

- The unified entity `World` owned by `RuntimeWorld` (`engine/include/world/RuntimeWorld.h:74`), with zone partitions; consumed by every scheduled system.
- Transform components `WorldTransform` and `WorldTransformHistory`, written by `PropagateTransforms` (`engine/include/world/transform/TransformPropagation.h:33`): consumed by physics, render, audio and attachments.
- Authority facts, `IsSimulationAuthority` (`engine/include/world/SimulationAuthority.h:38`) and `IsLocallyPredicted` (`engine/include/world/SimulationTimeline.h:18`): consumed by every system that must not run on a client.
- Persistent identity, `PersistentEntityIndex` (`engine/include/world/identity/PersistentEntityIndex.h:23`): consumed by zone state, replication and level logic.

## 4. Central decisions

- `ComponentRegistrar` (`engine/include/world/ComponentRegistrar.h:108`): decides which registries a component reaches and refuses a second type claiming a held identity.
- `LoadSceneJson` (`engine/src/world/serialization/SceneSerializer.cpp:158`): accepts or refuses an authored scene and builds its entities.
- `ParseContainer` (`engine/src/world/scene/SmapFormat.cpp:76`): accepts or refuses a cooked scene container.
- `PropagateTransforms` (`engine/include/world/transform/TransformPropagation.h:33`): decides which world transforms need recomputing from their parents.

## 5. Invariants

- A different component claiming a held identity is refused: `ComponentRegistrar.ADifferentComponentClaimingAHeldIdentityIsRefused` (`test/runtime/ComponentRegistrarTests.cpp`).
- One JSON key with a different type id is rejected: `SceneSerializerIdentity.SameJsonKeyDifferentTypeIdRejected` (`test/core/SceneSerializerIdentityTests.cpp`).
- A child inherits its parent's transform: `TransformPropagation.ChildInheritsParentInEcsWorld` (`test/runtime/TransformPropagationTests.cpp`).
- An importing zone stays hidden until atomic publication: `UnifiedRuntimeWorld.ImportingZoneIsHiddenUntilAtomicPublication` (`test/runtime/UnifiedRuntimeWorldTests.cpp`).
- A cancelled import leaves no partial state: `UnifiedRuntimeWorld.CancellingImportDestroysPartialStateWithoutResidency` (`test/runtime/UnifiedRuntimeWorldTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 6,364 lines in 53 files.
- CCN 28 `ParseContainer` (`engine/src/world/scene/SmapFormat.cpp:76`); CCN 26 `LoadSceneJson` (`engine/src/world/serialization/SceneSerializer.cpp:158`); CCN 23 `SmapWire::DecodeValue` (`engine/src/world/scene/SmapWire.h:326`).

## 7. Docs that contradict the code

- `docs/audio/runtime.md:79` places `EngineSceneComponents` in `world/ComponentManifest.h`; neither the symbol nor the file exists.
