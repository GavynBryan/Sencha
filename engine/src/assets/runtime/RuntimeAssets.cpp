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
                    std::make_unique<SkinnedMeshCache>(logging, GpuBuffers{ &buffers }),
                    nullptr, nullptr)
{
}

RuntimeAssets::RuntimeAssets(LoggingProvider& logging,
                             const ComponentSerializerRegistry& sceneSerializers)
    : RuntimeAssets(logging, sceneSerializers, nullptr, nullptr, nullptr, nullptr, nullptr)
{
}

RuntimeAssets::RuntimeAssets(LoggingProvider& logging,
                             const ComponentSerializerRegistry& sceneSerializers,
                             ReferenceOnly)
    : RuntimeAssets(logging, sceneSerializers, nullptr, nullptr, nullptr,
                    std::make_unique<StaticMeshReferenceStore>(),
                    std::make_unique<SkinnedMeshReferenceStore>())
{
}

RuntimeAssets::RuntimeAssets(LoggingProvider& logging,
                             const ComponentSerializerRegistry& sceneSerializers,
                             std::unique_ptr<TextureCache> textures,
                             std::unique_ptr<StaticMeshCache> staticMeshes,
                             std::unique_ptr<SkinnedMeshCache> skinnedMeshes,
                             std::unique_ptr<StaticMeshReferenceStore> staticMeshReferences,
                             std::unique_ptr<SkinnedMeshReferenceStore> skinnedMeshReferences)
    : Registry(logging)
    , Textures(std::move(textures))
    , MaterialSets(&Materials)
    , StaticMeshes(std::move(staticMeshes))
    , SkinnedMeshes(std::move(skinnedMeshes))
    , StaticMeshReferences(std::move(staticMeshReferences))
    , SkinnedMeshReferences(std::move(skinnedMeshReferences))
    , AudioClips(logging)
    , Scenes(logging)
    , Fonts(logging)
    , UiPackages(logging)
    , StaticMeshLoader(logging, StaticMeshes.get())
    , TextureLoader(logging, Textures.get())
    , MaterialLoader(logging, &Materials, Textures.get())
    , AudioClipLoader(logging, &AudioClips)
    , SkeletonLoader(logging, &Skeletons)
    , AnimationClipLoader(logging, &AnimationClips, &Skeletons)
    , SkinnedMeshLoader(logging, SkinnedMeshes.get(), &Skeletons)
    , SceneLoader(logging, &Scenes, &sceneSerializers)
    , DataLoader(logging, &DataTypes, &DataSchemas, &DataAssets)
    , FontLoader(logging, &Fonts)
    , UiPackageLoader(logging, &UiPackages)
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

    // A reference store is its own stager and store, so it takes the mesh
    // kind whole; the real loader stages against a cache this composition
    // does not have and is left out rather than registered beside it.
    if (StaticMeshReferences != nullptr)
        RegisterAssetKind(Assets, AssetType::StaticMesh, *StaticMeshReferences, StaticMeshReferences.get());
    else
        RegisterAssetKind(Assets, AssetType::StaticMesh, StaticMeshLoader, StaticMeshes.get());
    if (SkinnedMeshReferences != nullptr)
        RegisterAssetKind(Assets, AssetType::SkinnedMesh, *SkinnedMeshReferences, SkinnedMeshReferences.get());
    else
        RegisterAssetKind(Assets, AssetType::SkinnedMesh, SkinnedMeshLoader, SkinnedMeshes.get());
    RegisterAssetKind(Assets, AssetType::Material, MaterialLoader, &Materials, &MaterialSets);
    RegisterAssetKind(Assets, AssetType::Texture, TextureLoader, Textures.get());
    RegisterAssetKind(Assets, AssetType::Audio, AudioClipLoader, &AudioClips);
    RegisterAssetKind(Assets, AssetType::Skeleton, SkeletonLoader, &Skeletons);
    RegisterAssetKind(Assets, AssetType::AnimationClip, AnimationClipLoader, &AnimationClips);
    RegisterAssetKind(Assets, AssetType::Scene, SceneLoader, &Scenes);
    RegisterAssetKind(Assets, AssetType::Data, DataLoader, &DataAssets);
    // Fonts before packages, matching BuiltinAssetKinds(): a package declares
    // its faces as staging dependencies, and the preloader walks kinds in
    // registration order.
    RegisterAssetKind(Assets, AssetType::Font, FontLoader, &Fonts);
    RegisterAssetKind(Assets, AssetType::UiPackage, UiPackageLoader, &UiPackages);
}
