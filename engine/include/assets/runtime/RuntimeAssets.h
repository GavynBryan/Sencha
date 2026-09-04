#pragma once

#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <assets/animation/AnimationClipAssetLoader.h>
#include <assets/audio_clip/AudioClipAssetLoader.h>
#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetLoader.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <assets/material/MaterialAssetLoader.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/scene/SceneAssetLoader.h>
#include <assets/scene/SceneCache.h>
#include <assets/skeleton/SkeletonAssetLoader.h>
#include <assets/skinned_mesh/SkinnedMeshAssetLoader.h>
#include <assets/static_mesh/StaticMeshAssetLoader.h>
#include <assets/texture/TextureAssetLoader.h>
#include <assets/texture/TextureCache.h>
#include <audio/AudioClipCache.h>
#include <core/assets/AssetReferenceStore.h>
#include <core/assets/AssetRegistry.h>
#include <core/metadata/DataSchema.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <memory>

class ComponentSerializerRegistry;
class LoggingProvider;
class VulkanBufferService;
class VulkanDescriptorCache;
class VulkanImageService;
class VulkanSamplerCache;

// The engine's asset composition: every cache, the loader for each, and the
// front door they all register with.
//
// Declaration order is the destruction contract, and members are destroyed
// bottom-up. Assets goes first, so no registered commit or reload can run
// against a loader or cache that is already gone. The loaders go next; they
// hold nothing. Then the caches, each declared after every cache it holds
// references into, so a reference is always released into a cache that still
// exists:
//   Materials -> Textures,
//   MaterialSets -> Materials,
//   StaticMeshes, SkinnedMeshes, AnimationClips -> Skeletons.
//
// The three caches that own GPU resources are held by pointer because a
// process without graphics services does not have them: a dedicated host loads
// the same content through the same front door and simply cannot hold a mesh
// or a texture. Which of them exist is decided once, by which constructor ran,
// and never changes -- AssetSystem::HasStore is the query, and everything
// downstream treats a missing one as a capability this process was built
// without rather than as a failure.
//
// The reference-only composition is the third answer for the two mesh kinds
// scene content names directly: no cache, but a store that interns paths, so
// a process that only carries references (a headless cook writing a scene
// back out) resolves a mesh field to a handle and the handle back to its path
// without ever reading the mesh. HasStore is true for those kinds there, and
// nothing that draws is ever composed this way.
struct RuntimeAssets
{
    using StaticMeshReferenceStore =
        AssetReferenceStore<StaticMeshHandle, AssetType::StaticMesh>;
    using SkinnedMeshReferenceStore =
        AssetReferenceStore<SkinnedMeshHandle, AssetType::SkinnedMesh>;

    AssetRegistry Registry;
    std::unique_ptr<TextureCache> Textures;
    MaterialCache Materials;
    MaterialSetCache MaterialSets;
    SkeletonCache Skeletons;
    std::unique_ptr<StaticMeshCache> StaticMeshes;
    std::unique_ptr<SkinnedMeshCache> SkinnedMeshes;
    std::unique_ptr<StaticMeshReferenceStore> StaticMeshReferences;
    std::unique_ptr<SkinnedMeshReferenceStore> SkinnedMeshReferences;
    AnimationClipCache AnimationClips;
    AudioClipCache AudioClips;
    // Parsed cooked scenes, plain CPU data: resident in every composition,
    // windowed and headless alike -- a dedicated host spawns scenes too.
    SceneCache Scenes;

    // Structured data. The subtype registry and schemas are separate from the
    // cache because a game module registers into them while the module is
    // mapped, and must unregister before it is unmapped.
    DataAssetTypeRegistry DataTypes;
    DataSchemaRegistry DataSchemas;
    DataAssetCache DataAssets;

private:
    StaticMeshAssetLoader StaticMeshLoader;
    TextureAssetLoader TextureLoader;
    MaterialAssetLoader MaterialLoader;
    AudioClipAssetLoader AudioClipLoader;
    SkeletonAssetLoader SkeletonLoader;
    AnimationClipAssetLoader AnimationClipLoader;
    SkinnedMeshAssetLoader SkinnedMeshLoader;
    SceneAssetLoader SceneLoader;
    DataAssetLoader DataLoader;

public:
    AssetSystem Assets;

    // The windowed composition: every kind this engine knows is loadable.
    // `sceneSerializers` is the component vocabulary scene loads validate
    // against -- the host's one registry (Engine::SceneSerializers()), which
    // game modules extend in place.
    RuntimeAssets(LoggingProvider& logging,
                  VulkanBufferService& buffers,
                  VulkanImageService& images,
                  VulkanDescriptorCache& descriptors,
                  VulkanSamplerCache& samplers,
                  const ComponentSerializerRegistry& sceneSerializers);

    // The headless composition: no graphics services, so no cache can hold a
    // mesh or a texture. Everything else -- materials, material sets, skeletons,
    // animation clips, audio, scenes, and the whole structured-data stack --
    // loads exactly as it does windowed.
    RuntimeAssets(LoggingProvider& logging,
                  const ComponentSerializerRegistry& sceneSerializers);

    // The reference-only composition: headless, and the static and skinned
    // mesh kinds resolve to path-only handles instead of declining. For a
    // process that reads scenes and writes them back -- the headless cook --
    // and never for one that simulates or draws, where a mesh handle that
    // names geometry nothing loaded would be a lie every consumer believes.
    struct ReferenceOnly {};
    RuntimeAssets(LoggingProvider& logging,
                  const ComponentSerializerRegistry& sceneSerializers,
                  ReferenceOnly);

    RuntimeAssets(const RuntimeAssets&) = delete;
    RuntimeAssets& operator=(const RuntimeAssets&) = delete;
    RuntimeAssets(RuntimeAssets&&) = delete;
    RuntimeAssets& operator=(RuntimeAssets&&) = delete;

private:
    RuntimeAssets(LoggingProvider& logging,
                  const ComponentSerializerRegistry& sceneSerializers,
                  std::unique_ptr<TextureCache> textures,
                  std::unique_ptr<StaticMeshCache> staticMeshes,
                  std::unique_ptr<SkinnedMeshCache> skinnedMeshes,
                  std::unique_ptr<StaticMeshReferenceStore> staticMeshReferences,
                  std::unique_ptr<SkinnedMeshReferenceStore> skinnedMeshReferences);
};
