#pragma once

#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetLoader.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <audio/AudioClipCache.h>
#include <core/metadata/DataSchema.h>
#include <input/InputProfileData.h>
#include <movement/MovementProfileData.h>
#include <core/assets/AssetRegistry.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/scene/SceneCache.h>
#include <assets/texture/TextureCache.h>
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

// Declaration order is load-bearing — caches that hold RAII references into
// other caches must be declared after them so they are destroyed first:
//   Materials → Textures (so Textures is declared first),
//   StaticMeshes/SkinnedMeshes → Skeletons and AnimationClips → Skeletons (so
//   Skeletons is declared before all three, destroyed after them, Stage 5).
//
// The three caches that own GPU resources are held by pointer because a
// process without graphics services does not have them: a dedicated host loads
// the same content through the same front door and simply cannot hold a mesh
// or a texture. Which of them exist is decided once, by which constructor ran,
// and never changes -- AssetSystem::HasStore is the query, and everything
// downstream treats a missing one as a capability this process was built
// without rather than as a failure.
struct RuntimeAssets
{
    AssetRegistry Registry;
    std::unique_ptr<TextureCache> Textures;
    MaterialCache Materials;
    // After Materials so it is destroyed first: a set releases a reference to
    // each member material on teardown, which must outlive it.
    MaterialSetCache MaterialSets;
    SkeletonCache Skeletons;
    std::unique_ptr<StaticMeshCache> StaticMeshes;
    std::unique_ptr<SkinnedMeshCache> SkinnedMeshes;
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
    DataAssetLoader DataLoader;

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
                  const ComponentSerializerRegistry& sceneSerializers)
        : RuntimeAssets(logging, sceneSerializers,
                        std::make_unique<TextureCache>(logging, images, descriptors, samplers),
                        std::make_unique<StaticMeshCache>(logging, GpuBuffers{&buffers}),
                        std::make_unique<SkinnedMeshCache>(logging, GpuBuffers{&buffers}))
    {
    }

    // The headless composition: no graphics services, so no cache can hold a
    // mesh or a texture. Everything else -- materials, material sets, skeletons,
    // animation clips, audio, scenes, and the whole structured-data stack --
    // loads exactly as it does windowed.
    RuntimeAssets(LoggingProvider& logging,
                  const ComponentSerializerRegistry& sceneSerializers)
        : RuntimeAssets(logging, sceneSerializers, nullptr, nullptr, nullptr)
    {
    }

private:
    RuntimeAssets(LoggingProvider& logging,
                  const ComponentSerializerRegistry& sceneSerializers,
                  std::unique_ptr<TextureCache> textures,
                  std::unique_ptr<StaticMeshCache> staticMeshes,
                  std::unique_ptr<SkinnedMeshCache> skinnedMeshes)
        : Registry(logging)
        , Textures(std::move(textures))
        , Materials()
        , MaterialSets(&Materials)
        , Skeletons()
        , StaticMeshes(std::move(staticMeshes))
        , SkinnedMeshes(std::move(skinnedMeshes))
        , AnimationClips()
        , AudioClips(logging)
        , Scenes(logging)
        , DataTypes()
        , DataSchemas()
        , DataAssets()
        , DataLoader(logging, &DataTypes, &DataSchemas, &DataAssets)
        , Assets(logging, Registry, StaticMeshes.get(), &Materials, Textures.get(),
                 &AudioClips, &Skeletons, &AnimationClips, SkinnedMeshes.get(),
                 &MaterialSets, &Scenes, &sceneSerializers)
    {
        // Unregistering a subtype with values still resident would leave the
        // cache holding a value nothing can interpret.
        DataTypes.SetResidentQuery([this](std::string_view typeName)
        {
            return DataAssets.HasResidentSubtype(typeName);
        });

        // The engine's own data subtypes. A game module adds its own through
        // the same registry via Game::OnRegisterDataAssetTypes, which is what
        // makes them appear in the prebuilt Data Editor.
        RegisterMovementProfileData(DataTypes, DataSchemas);
        RegisterInputProfileData(DataTypes, DataSchemas);

        // Data is the one built-in kind AssetSystem cannot register itself:
        // its cache and loader live here, not in the front door.
        AssetKindRegistration data = MakeBuiltinAssetKind(AssetType::Data);
        data.Stager = &DataLoader;
        data.Store = &DataAssets;
        data.Commit = [this](AssetStaging&& staged) -> AssetLease
        {
            const DataAssetHandle handle = DataLoader.CommitTyped(std::move(staged));
            if (!handle.IsValid())
                return {};
            return AssetLease::Adopt(AssetType::Data, DataAssets, handle.ToToken());
        };
        data.Reload = [this](AssetStaging&& staged)
        {
            return DataLoader.CommitReload(std::move(staged));
        };
        (void)Assets.Kinds().Register(std::move(data));
    }

public:
    RuntimeAssets(const RuntimeAssets&) = delete;
    RuntimeAssets& operator=(const RuntimeAssets&) = delete;

    RuntimeAssets(RuntimeAssets&&) = delete;
    RuntimeAssets& operator=(RuntimeAssets&&) = delete;
};
