# anim

Snapshot `97ed48e4` (2026-10-30). Layer: motion and space.

## 1. Frame phases

- `Simulate` (FixedLogic lane): five systems in order, from `AnimRigCompositionSystem` (`engine/src/anim/AnimationRegistration.cpp:217`) to `AnimEventSystem` (`engine/src/anim/AnimationRegistration.cpp:221`).
- `Simulate` (PostFixed lane): `AnimPoseSystem` (`engine/src/anim/AnimationRegistration.cpp:239`) samples poses only when the host presents.
- `RegisterAnimationSystems` (`engine/src/app/Engine.cpp:1443`) is the engine's call site.

## 2. Inputs

- Facts from gameplay, `AnimFacts` (`engine/include/anim/AnimFacts.h:14`), gathered through `AnimFactProviders` (`engine/include/anim/AnimFactProviders.h:85`).
- Requests from abilities and verbs, `AnimRequestSet` (`engine/include/anim/AnimRequestSet.h:75`).
- Authored rigs, selectors, flows and blendspaces, compiled from data assets such as `AnimFlowData` (`engine/include/anim/AnimFlowData.h:59`).
- Module dependencies: abilities, app, assets, authored, core, ecs, gameplay_tags, jobs, math, movement, net, prediction, time, world.

## 3. Outputs

- `AnimPoseState` (`engine/include/anim/AnimPoseState.h:50`): the sampled skeleton pose, consumed by render skinning and attachments.
- Animation events, drained by `DrainAnimEvents` (`engine/include/anim/AnimEventSystem.h:94`): consumed by the verb dispatcher.
- Root motion samples: consumed by movement's `RootMotionSystem`.
- `AnimDecisionLog`: why each layer chose what it did, consumed by the animation preview and inspectors.

## 4. Central decisions

- `SelectAnimLayer` (`engine/src/anim/AnimSelectSystem.cpp:103`): picks each layer's winning entry from facts, requests, latches and interrupts.
- `CollectAnimEvents` (`engine/src/anim/AnimEventSystem.cpp:58`): decides which marks fire this tick and where each event is produced.
- `Compile` (`engine/src/anim/AnimRigData.cpp:145`): accepts or refuses an authored rig.

## 5. Invariants

- A cyclic mark fires once per loop: `AnimEvents.ACyclicMarkFiresOncePerLoop` (`test/runtime/AnimEventTests.cpp`).
- A cosmetic event cannot bind a gameplay verb: `AnimEventBinding.ACosmeticEventCannotBindAGameplayVerb` (`test/runtime/AnimEventTests.cpp`).
- A client never originates gameplay events: `AnimReplication.AClientNeverOriginatesGameplayEvents` (`test/runtime/AnimReplicationTests.cpp`).
- A correction is absorbed without rewinding history: `AnimReplication.ACorrectionIsAbsorbedWithoutRewindingHistory` (`test/runtime/AnimReplicationTests.cpp`).
- A pose slot belongs only to the entity it was assigned to: `AnimPose.APoseSlotIsOnlyTheEntitysItWasAssignedTo` (`test/runtime/AnimPoseTests.cpp`).
- An inertialized offset decays from what was shown to rest: `AnimInertialization.AnOffsetDecaysFromWhatWasShownToRest` (`test/runtime/AnimPoseTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 14,695 lines in 118 files.
- CCN 88 `SelectAnimLayer` (`engine/src/anim/AnimSelectSystem.cpp:103`); CCN 76 `CollectAnimEvents` (`engine/src/anim/AnimEventSystem.cpp:58`); CCN 49 `Compile` (`engine/src/anim/AnimRigData.cpp:145`).

## 7. Docs that contradict the code

- `docs/assets/pipeline.md:656` names `ReadAnimationClip`; no such symbol exists (cooked clips load through `LoadSanimFromBytes`).
