# abilities

Snapshot `97ed48e4` (2026-10-30). Layer: gameplay simulation.

## 1. Frame phases

- `Simulate` (FixedLogic lane): `RunFixedLogic` (`engine/src/app/EngineFramePhases.cpp:791`) runs `AbilitySystem` (`engine/src/abilities/AbilityKit.cpp:68`) after `ActorTimeSystem`.
- `Simulate` (FixedLogic lane): `AbilityPresentationSystem` (`engine/src/abilities/AbilityKit.cpp:75`) runs after `AbilitySystem` only when the host presents.
- `RegisterAbilityKitSystems` (`engine/src/app/Engine.cpp:1424`) is the engine's call site.

## 2. Inputs

- What an actor may run, `AbilityGrants` (`engine/include/abilities/AbilityGrants.h:44`), and named starts and cancels, `AbilityRequestQueue` (`engine/include/abilities/AbilityRequestQueue.h:29`).
- Compiled definitions, `AbilityRegistry` (`engine/include/abilities/AbilityRegistry.h:18`), built from content by `CompileAbilityDeclaration` (`engine/include/abilities/AbilityDefinitionData.h:20`).
- What operations reach, `AbilityHost` (`engine/include/abilities/AbilitySystem.h:17`): the verb dispatcher, queries and leases.
- Module dependencies: anim, app, assets, attributes, authored, controller, core, ecs, effects, gameplay_tags, input, math, net, prediction, time, world.

## 3. Outputs

- `ActiveAbilities` (`engine/include/abilities/ActiveAbilities.h:126`): running and lingering records, consumed by presentation, replication and the editor preview.
- `AbilityCueLog` (`engine/include/abilities/AbilityCueLog.h:33`): discrete cues, consumed by `AbilityPresentationSystem` and replication.
- `AbilityTrace` (`engine/include/abilities/AbilityTrace.h:70`): why an ability did or did not run, consumed by inspectors.
- Effects and attribute costs, applied through `ApplyEffect` (`engine/include/effects/EffectSystem.h:17`).

## 4. Central decisions

- `RunAbilityPass` (`engine/src/abilities/AbilitySystem.cpp:289`): admits, steps and ends every activation for one fixed tick, on the authority or as a guess.
- `AbilityPass::Holds` (`engine/src/abilities/AbilityConditions.cpp:196`): evaluates one authored condition against an actor.
- `Reader::Condition` (`engine/src/abilities/AbilityDefinitionData.cpp:346`): accepts or refuses one authored condition while compiling.

## 5. Invariants

- Out-of-order snapshots neither duplicate nor rewind: `AbilityReplication.OutOfOrderSnapshotsDoNotDuplicateOrRewind` (`test/runtime/AbilityReplicationTests.cpp`).
- The owner does not present a predicted cue twice: `AbilityReplication.OwnerDoesNotPresentPredictedCuesTwice` (`test/runtime/AbilityReplicationTests.cpp`).
- A guess runs only what its prediction level allows: `AbilityReplication.AGuessRunsOnlyWhatItsPredictionLevelAllows` (`test/runtime/AbilityReplicationTests.cpp`).
- A discrete cue is presented once: `AbilityPresentation.ADiscreteCueIsLoggedAndPresentedOnce` (`test/runtime/AbilityPresentationTests.cpp`).
- Each structural problem in a definition is named with its element: `AbilityDefinitionData.EachStructuralProblemIsNamedWithItsElement` (`test/framework/AbilityDefinitionDataTests.cpp`).
- Only authored grants are saved: `AbilityGrantsSerializer.OnlyAuthoredGrantsAreSaved` (`test/runtime/AbilityGrantsSerializerTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 7,750 lines in 57 files.
- CCN 37 `AbilityPass::Holds` (`engine/src/abilities/AbilityConditions.cpp:196`); CCN 37 `RunAbilityPass` (`engine/src/abilities/AbilitySystem.cpp:289`); CCN 34 `Reader::Condition` (`engine/src/abilities/AbilityDefinitionData.cpp:346`).

## 7. Docs that contradict the code

- `docs/gameplay/abilitykit.md:77` describes a queued `DeathEvent` notification; no such type exists in the tree.
