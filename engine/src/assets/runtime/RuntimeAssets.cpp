#include <assets/runtime/RuntimeAssets.h>

#include <assets/runtime/RegisterAssetKind.h>
#include <core/logging/LoggingProvider.h>
#include <input/InputProfileData.h>
#include <movement/MovementProfileData.h>

#include <string_view>
#include <utility>

RuntimeAssets::RuntimeAssets(LoggingProvider& logging,
                             VulkanBufferService& buffers,
                             VulkanImageService& images,
                             VulkanDescriptorCache& descriptors,
                             VulkanSamplerCache& samplers,
                             const ComponentSerializerRegistry& sceneSerializers)
    : RuntimeAssets(logging, sceneSerializers,
                    std::make_unique<TextureCache>(logging, images, descriptors, samplers),
                    std::make_unique<StaticMeshCache>(logging, GpuBuffers{ &buffers }),
                    std::make_unique<SkinnedMeshCache>(logging, GpuBuffers{ &buffers }))
{
}

RuntimeAssets::RuntimeAssets(LoggingProvider& logging,
                             const ComponentSerializerRegistry& sceneSerializers)
    : RuntimeAssets(logging, sceneSerializers, nullptr, nullptr, nullptr)
{
}

RuntimeAssets::RuntimeAssets(LoggingProvider& logging,
                             const ComponentSerializerRegistry& sceneSerializers,
                             std::unique_ptr<TextureCache> textures,
                             std::unique_ptr<StaticMeshCache> staticMeshes,
                             std::unique_ptr<SkinnedMeshCache> skinnedMeshes)
    : Registry(logging)
    , Textures(std::move(textures))
    , MaterialSets(&Materials)
    , StaticMeshes(std::move(staticMeshes))
    , SkinnedMeshes(std::move(skinnedMeshes))
    , AudioClips(logging)
    , Scenes(logging)
    , StaticMeshLoader(logging, StaticMeshes.get())
    , TextureLoader(logging, Textures.get())
    , MaterialLoader(logging, &Materials, Textures.get())
    , AudioClipLoader(logging, &AudioClips)
    , SkeletonLoader(logging, &Skeletons)
    , AnimationClipLoader(logging, &AnimationClips, &Skeletons)
    , SkinnedMeshLoader(logging, SkinnedMeshes.get(), &Skeletons)
    , SceneLoader(logging, &Scenes, &sceneSerializers)
    , DataLoader(logging, &DataTypes, &DataSchemas, &DataAssets)
    , Assets(logging, Registry)
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

    RegisterAssetKind(Assets, AssetType::StaticMesh, StaticMeshLoader, StaticMeshes.get());
    RegisterAssetKind(Assets, AssetType::SkinnedMesh, SkinnedMeshLoader, SkinnedMeshes.get());
    RegisterAssetKind(Assets, AssetType::Material, MaterialLoader, &Materials, &MaterialSets);
    RegisterAssetKind(Assets, AssetType::Texture, TextureLoader, Textures.get());
    RegisterAssetKind(Assets, AssetType::Audio, AudioClipLoader, &AudioClips);
    RegisterAssetKind(Assets, AssetType::Skeleton, SkeletonLoader, &Skeletons);
    RegisterAssetKind(Assets, AssetType::AnimationClip, AnimationClipLoader, &AnimationClips);
    RegisterAssetKind(Assets, AssetType::Scene, SceneLoader, &Scenes);
    RegisterAssetKind(Assets, AssetType::Data, DataLoader, &DataAssets);
}
