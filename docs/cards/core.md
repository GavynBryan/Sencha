# core

Snapshot `97ed48e4` (2026-10-30). Layer: foundation.

## 1. Frame phases

- None of its own: a library under every module; console lines fed from stdin execute in `PumpPlatform` (`engine/src/app/EngineFramePhases.cpp:147`).

## 2. Inputs

- Engine config JSON, read by `LoadEngineConfig` (`engine/include/core/config/EngineConfig.h:55`).
- Console command lines and cvar assignments, parsed by `ParseCVarValue` (`engine/include/core/console/ConsoleRegistry.h:144`).
- Asset files on disk, registered by `RegisterAssetFile` (`engine/include/core/assets/AssetRegistry.h:106`).
- Module dependencies: audio (`engine/include/core/config/AudioConfig.h:3`) and platform (`engine/include/core/config/WindowConfig.h:4`).

## 3. Outputs

- Binary and JSON archives, `BinaryWriteArchive` (`engine/include/core/serialization/BinaryArchive.h:13`) and `JsonWriteArchive` (`engine/include/core/serialization/JsonArchive.h:15`): the binary archive is used inside core's serializer, the JSON archive by world's scene serializer and 4 editor files.
- Strong ids and handles, `StrongId` (`engine/include/core/identity/StrongId.h:26`) and `HandlePool` (`engine/include/core/handle/HandlePool.h:16`): used across 34 engine modules and 238 editor files.
- Content hashes, `HashFnv1aBytes` (`engine/include/core/hash/Fnv1a.h:41`): consumed by logic, render and 3 editor files.
- Schemas, `DataSchema` (`engine/include/core/metadata/DataSchema.h:127`) and `RuntimeField` (`engine/include/core/metadata/RuntimeSchema.h:48`): consumed by the editor inspector, data assets and replication layouts.

## 4. Central decisions

- `NormalizeDataValue` (`engine/src/core/metadata/DataSchema.cpp:141`): accepts, defaults or refuses each member of an authored value against its schema.
- `DeserializeRuntimeConfig` (`engine/src/core/config/RuntimeConfig.cpp:103`): accepts or refuses runtime settings such as tick rate and worker counts.
- `ParseCVarValue` (`engine/include/core/console/ConsoleRegistry.h:144`): turns console text into a typed cvar value or an error.
- `RegisterAssetFile` (`engine/include/core/assets/AssetRegistry.h:106`): binds an asset path to its stated or derived id.

## 5. Invariants

- A truncated stream fails the read: `SerializationTests.ReadFailsWhenStreamIsTruncated` (`test/core/SerializationTests.cpp`).
- JSON reads ignore unknown fields and round-trip schema records: `SerializationTests.SchemaJsonRoundTripsStandaloneRecordAndIgnoresUnknownFields` (`test/core/SerializationTests.cpp`).
- Unknown authored members are refused unless the rules allow them: `DataValueNormalize.UnknownMembersAreRefusedUnlessTheRulesAllowThem` (`test/core/DataValueNormalizeTests.cpp`).
- A settings archive can never run a command: `ArchiveFixture.AnArchiveEntryCannotInvokeACommand` (`test/core/CVarArchiveTests.cpp`).
- A non-positive tick rate is refused: `RuntimeConfig.RejectsNonPositiveTickRate` (`test/core/RuntimeConfigTests.cpp`).
- One asset never takes another asset's id: `AssetRegistryIds.AssignRebindsButNeverTakesAnotherAssetsId` (`test/core/AssetIdTests.cpp`).
- One flipped bit changes a content hash: `ContentHash.SingleBitChangeChangesHash` (`test/core/ContentHashTests.cpp`).

### Unchecked

- Core includes nothing above itself: it includes `audio/AudioBus.h` and `platform/WindowTypes.h`, and `scripts/check_engine_layering.sh` covers named boundaries only, not core's includes.

## 6. Size

- 12,254 lines in 123 files.
- CCN 65 `NormalizeDataValue` (`engine/src/core/metadata/DataSchema.cpp:141`); CCN 46 `DeserializeRuntimeConfig` (`engine/src/core/config/RuntimeConfig.cpp:103`); CCN 31 `DeserializeAudioConfig` (`engine/src/core/config/AudioConfig.cpp:26`).

## 7. Docs that contradict the code

- `docs/core-systems-map.md:393` says `CollectAssetPaths` walks scenes for the asset manifest; no such function exists in the tree.
