# effects

Snapshot `97ed48e4` (2026-10-30). Layer: gameplay simulation.

## 1. Frame phases

- `Simulate` (FixedLogic lane): `RunFixedLogic` (`engine/src/app/EngineFramePhases.cpp:791`) runs `AttributeResolveSystem` (`engine/src/abilities/AbilityKit.cpp:69`) after `AbilitySystem`.
- `Simulate` (FixedLogic lane): `EffectLifetimeSystem` (`engine/src/abilities/AbilityKit.cpp:70`) advances and ends lasting effects.

## 2. Inputs

- Compiled definitions, `EffectDefinition` (`engine/include/effects/EffectDefinition.h:52`) in `EffectRegistry` (`engine/include/effects/EffectRegistry.h:18`).
- Targets' `AttributeSet` (`engine/include/attributes/AttributeSet.h:18`) and gameplay tags.
- Requests from abilities, projectiles and verbs, through `EffectOperations` (`engine/include/effects/EffectVerbs.h:23`).
- Module dependencies: abilities, app, assets, attributes, authored, core, ecs, gameplay_tags, time.

## 3. Outputs

- `ActiveEffect` (`engine/include/effects/ActiveEffect.h:19`): one entity per lasting effect, consumed by the fold and by ability grants.
- Current values in each target's `AttributeSet`, folded by `ResolveAttributesWithEffects` (`engine/include/effects/EffectSystem.h:29`): read by abilities, movement, projectile and debug.
- Granted tags, ref-counted on the target: consumed by any tag query.
- Cost verdicts, `CheckEffectCosts` (`engine/include/effects/EffectCost.h:39`): consumed by ability admission.

## 4. Central decisions

- `ApplyEffect` (`engine/src/effects/EffectSystem.cpp:216`): applies an instant change or creates a lasting effect on a target.
- `TickEffectsImpl` (`engine/src/effects/EffectSystem.cpp:106`): advances periodic effects and ends those whose time, owner or definition is gone.
- `Compile` (`engine/src/effects/EffectDefinitionData.cpp:100`): accepts or refuses an authored effect.

## 5. Invariants

- A duration ends exactly on its end tick: `EffectFixture.DurationEndsExactlyOnItsEndTick` (`test/framework/EffectTests.cpp`).
- The fold applies add, then multiply, then override: `EffectFixture.FoldAppliesAddThenMultiplyThenOverride` (`test/framework/EffectTests.cpp`).
- The fold is identical whatever order storage holds: `EffectFold.TheFoldIsIdenticalWhateverOrderStorageHolds` (`test/framework/EffectTests.cpp`).
- Granted tags are ref-counted across stacked effects: `EffectFixture.GrantedTagsAreRefCountedAcrossStackedEffects` (`test/framework/EffectTests.cpp`).
- A cost fails closed on anything missing or inexact: `EffectCost.ACostFailsClosedOnAnythingMissingOrInexact` (`test/framework/EffectTests.cpp`).
- An instant effect cannot be held: `EffectVerbTest.AnInstantEffectCannotBeHeld` (`test/framework/EffectVerbTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 1,215 lines in 17 files.
- CCN 27 `Compile` (`engine/src/effects/EffectDefinitionData.cpp:100`); CCN 19 `ApplyEffect` (`engine/src/effects/EffectSystem.cpp:216`); CCN 18 `TickEffectsImpl` (`engine/src/effects/EffectSystem.cpp:106`).

## 7. Docs that contradict the code

- None found.
