# attributes

Snapshot `97ed48e4` (2026-10-30). Layer: gameplay simulation.

## 1. Frame phases

- None of its own; attributes are resolved each tick by `AttributeResolveSystem` (`engine/src/abilities/AbilityKit.cpp:69`), which belongs to effects.

## 2. Inputs

- Content declarations, `AttributeDeclaration` (`engine/include/attributes/AttributeDeclarations.h:18`), registered through `DeclareAttributes` (`engine/include/attributes/AttributeDeclarations.h:35`).
- Module dependencies: assets, core, ecs, net, world.

## 3. Outputs

- `AttributeRegistry` (`engine/include/attributes/AttributeRegistry.h:20`): names, defaults and ranges, consumed by effects, abilities, movement and assets.
- `AttributeSet` (`engine/include/attributes/AttributeSet.h:18`): an entity's base and current values, consumed by effects, abilities, movement, projectile and debug.
- `AttributeSetWire` (`engine/include/attributes/AttributeSetWire.h:22`): an actor's attributes as replication sends them to its owner alone.
- Saved values, `WriteAttributes` (`engine/include/attributes/AttributeSerialization.h:17`): the component's saved form, by name.

## 4. Central decisions

- `Compile` (`engine/src/attributes/AttributeDeclarations.cpp:54`): accepts or refuses one declared attribute.
- `AttributeSet::Add` (`engine/src/attributes/AttributeSet.cpp:80`): inserts a value at its sorted position, or refuses a full set.
- `ClampAttributes` (`engine/include/attributes/AttributeResolve.h:8`): clamps current values to the registered range.

## 5. Invariants

- A set stays sorted on add: `AttributeSet.AddSetGetStaysSorted` (`test/framework/AttributeTests.cpp`).
- Removal closes the gap and keeps the block sorted: `AttributeSet.RemoveClosesTheGapAndKeepsTheBlockSorted` (`test/framework/AttributeTests.cpp`).
- Current values clamp from base through the registry: `AttributeResolve.ClampsCurrentFromBaseViaRegistry` (`test/framework/AttributeTests.cpp`).
- Saved values round-trip by name across differing registries: `AttributeSerialization.RoundTripsByNameAcrossDifferingRegistries` (`test/framework/AttributeSerializationTests.cpp`).
- Unknown attributes are skipped, not fatal: `AttributeSerialization.UnknownAttributesAreSkippedNotFatal` (`test/framework/AttributeSerializationTests.cpp`).
- A second, different declaration of one attribute is reported and the first stands: `ContentVocabulary.AnAttributeDeclaredTwiceDifferentlyIsReportedAndTheFirstStands` (`test/runtime/ContentVocabularyTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 933 lines in 15 files.
- CCN 13 `Compile` (`engine/src/attributes/AttributeDeclarations.cpp:54`); CCN 7 `AttributeSet::Add` (`engine/src/attributes/AttributeSet.cpp:80`); CCN 6 `DeclareAttributes` (`engine/src/attributes/AttributeDeclarations.cpp:96`).

## 7. Docs that contradict the code

- None found.
