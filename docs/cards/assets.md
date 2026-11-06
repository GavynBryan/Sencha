# assets

Snapshot `97ed48e4` (2026-10-30). Layer: world and frame.

## 1. Frame phases

- `DrainAsyncTasks`: staged loads commit on the owner thread at `engine/src/app/EngineFramePhases.cpp:713`, in dependency order through `AssetPreloader::CommitReady` (`engine/include/assets/runtime/AssetPreloader.h:167`).
- Cooking runs outside the frame, in editors and tools, behind `SENCHA_ENABLE_COOK`.

## 2. Inputs

- Source files (glTF, Blender, images, audio, fonts), read through importers such as `ImportGltfScene` (`engine/src/assets/cook/MeshCook.cpp:1233`).
- Cooked artifacts and the cache index, `CookedArtifact` (`engine/include/assets/cook/CookedCache.h:35`).
- The staging contract, `IAssetStager` (`engine/include/core/assets/AssetStager.h:63`).
- Module dependencies: 19 engine modules, including abilities, anim, render and navigation, because their asset kinds load here.

## 3. Outputs

- `RuntimeAssets` (`engine/include/assets/runtime/RuntimeAssets.h:69`): resident meshes, materials, textures, clips and data assets, consumed by app, input and 68 editor files; the module as a whole is used by 21 engine modules.
- Cooked files and cache entries, from `ImportAssetsOnDemand` (`engine/include/assets/cook/ImportOnDemand.h:40`): read back by the runtime loaders.
- Packed lightmap atlases, `PackLightmapAtlas` (`engine/include/assets/cook/LightmapAtlasPack.h:49`): called from the editor's lighting bake.

## 4. Central decisions

- `AssetPreloader::CommitReady` (`engine/include/assets/runtime/AssetPreloader.h:167`): decides which staged assets commit, dependencies first.
- `ImportAssetsOnDemand` (`engine/include/assets/cook/ImportOnDemand.h:40`): decides which sources are stale and re-imports only those.
- `MeshLoader::LoadFromReader` (`engine/src/assets/static_mesh/MeshLoader.cpp:211`): accepts or refuses a cooked mesh.
- `LoadSanimFromBytes` (`engine/src/assets/animation/AnimationClipSerializer.cpp:200`): accepts or refuses a cooked animation clip.

## 5. Invariants

- A declared dependency commits before its dependent: `AssetPreload.DeclaredDependencyCommitsBeforeItsDependent` (`test/runtime/AssetPreloadTests.cpp`).
- A failed dependency fails its dependent without deadlock: `AssetPreload.FailedDependencyFailsItsDependentWithoutDeadlock` (`test/runtime/AssetPreloadTests.cpp`).
- Texture pixel ranges that overflow the header arithmetic are refused: `StexRoundTrip.RejectsPixelRangesThatOverflowTheHeaderArithmetic` (`test/core/TextureAssetTests.cpp`).
- A re-cook replaces the cache entry for the same source: `CookedCacheIndex.PutReplacesEntryForSameSource` (`test/core/AssetImportTests.cpp`).
- A glTF that references an external buffer is refused: `MeshCook.ExternalBufferUriIsRejected` (`test/core/MeshCookTests.cpp`).

### Unchecked

- Loaders open no files of their own (CLAUDE.md, "Computation and IO"): no check covers it.

## 6. Size

- 19,331 lines in 174 files.
- CCN 67 `ImportGltfScene` (`engine/src/assets/cook/MeshCook.cpp:1233`); CCN 61 `LoadSanimFromBytes` (`engine/src/assets/animation/AnimationClipSerializer.cpp:200`); CCN 47 `MeshLoader::LoadFromReader` (`engine/src/assets/static_mesh/MeshLoader.cpp:211`).

## 7. Docs that contradict the code

- `docs/assets/architecture.md:395` says to follow `OnAssetCommitted` for the commit path; no such symbol exists in the tree.
- Outside `docs/`: `CLAUDE.md:70` lists `IAssetLoader` and `ComponentManifest` as shared mechanisms to reuse; neither exists in the tree, and the staging contract is `IAssetStager`.
