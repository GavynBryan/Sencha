# gameplay_tags

Snapshot `97ed48e4` (2026-10-30). Layer: foundation.

## 1. Frame phases

- None of its own: a registry resource and containers read and written by scheduled systems in `Simulate`; the registry is filled at content load in `engine/src/app/RuntimeContent.cpp`.

## 2. Inputs

- Dotted tag names from content and code, compiled by `RegisterGameplayTagDeclarations` (`engine/include/gameplay_tags/GameplayTagDeclarations.h:25`).
- Tag queries as content spells them, `GameplayTagQueryNames` (`engine/include/gameplay_tags/GameplayTagQueryNames.h:12`), resolved by `ResolveTagQuery` (`engine/include/gameplay_tags/GameplayTagQueryNames.h:24`).
- Module dependencies: assets, core, ecs, net, world.

## 3. Outputs

- `GameplayTagRegistry` (`engine/include/gameplay_tags/GameplayTagRegistry.h:21`): interned ids and hierarchy, consumed by 14 engine modules and 20 editor files.
- `GameplayTagContainer` (`engine/include/gameplay_tags/GameplayTagContainer.h:30`), a component holding an entity's tags with stack counts: consumed by abilities, effects, perception, projectile and spatial queries.
- Wire forms, `GameplayTagWireShared` (`engine/include/gameplay_tags/GameplayTagContainerWire.h:29`): the replicated form.

## 4. Central decisions

- `MatchesQuery` (`engine/src/gameplay_tags/GameplayTagQuery.cpp:22`): decides whether a tag set satisfies All, Any and None clauses, exactly or hierarchically.
- `ValidateGameplayTagName` (`engine/src/gameplay_tags/GameplayTagRegistry.cpp:15`): accepts or refuses a dotted name.
- `Compile` (`engine/src/gameplay_tags/GameplayTagDeclarations.cpp:46`): turns declared names into registered ids, parents included.

## 5. Invariants

- Registering a child creates its parents: `GameplayTagRegistry.RegistersHierarchyAndAutoCreatesParents` (`test/framework/GameplayTagTests.cpp`).
- Overlapping grants need every source to revoke: `CountedGameplayTagSet.OverlappingGrantsRequireEverySourceToRevoke` (`test/framework/GameplayTagTests.cpp`).
- A wire key names the same tag whatever the registration order: `GameplayTagWireKey.NamesTheSameTagWhateverOrderItRegisteredIn` (`test/framework/GameplayTagTests.cpp`).
- Serialized tags round-trip by name across differing registries: `GameplayTagSerialization.RoundTripsByNameAcrossDifferingRegistries` (`test/framework/GameplayTagSerializationTests.cpp`).
- A container stays sorted regardless of insert order: `GameplayTagContainer.StaysSortedRegardlessOfInsertOrder` (`test/framework/GameplayTagContainerTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 1,560 lines in 25 files.
- CCN 14 `Compile` (`engine/src/gameplay_tags/GameplayTagDeclarations.cpp:46`); CCN 11 `ValidateGameplayTagName` (`engine/src/gameplay_tags/GameplayTagRegistry.cpp:15`); CCN 9 `MatchesQuery` (`engine/src/gameplay_tags/GameplayTagQuery.cpp:22`).

## 7. Docs that contradict the code

- None found.
