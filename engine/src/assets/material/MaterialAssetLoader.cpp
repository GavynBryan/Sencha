#include <assets/material/MaterialAssetLoader.h>

#include <assets/material/MaterialLoader.h>
#include <core/assets/AssetSystem.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/TextureCache.h>
#include <render/MaterialCache.h>

#include <format>
#include <optional>
#include <string>
#include <utility>

MaterialAssetLoader::MaterialAssetLoader(LoggingProvider& logging,
                                         AssetSystem& assets,
                                         MaterialCache* materials,
                                         TextureCache* textures)
    : Log(logging.GetLogger<MaterialAssetLoader>())
    , Assets(assets)
    , Materials(materials)
    , Textures(textures)
{
}

AssetStaging MaterialAssetLoader::LoadStaged(const AssetRecord& record, IAssetSource& source)
{
    AssetStaging staging;
    staging.Record = record;

    std::vector<std::byte> bytes;
    if (!ReadAssetBytes(source, record, bytes))
    {
        staging.Error = std::format("could not read material source for '{}'", record.Path);
        return staging;
    }

    JsonParseError jsonError;
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const std::optional<JsonValue> root = JsonParse(text, &jsonError);
    if (!root.has_value())
    {
        staging.Error = std::format("material JSON parse error at {}: {}",
                                    jsonError.Position, jsonError.Message);
        return staging;
    }

    MaterialDescription desc;
    MaterialParseError parseError;
    if (!ParseMaterialJson(*root, desc, &parseError))
    {
        staging.Error = parseError.Message;
        return staging;
    }

    staging.Payload = std::move(desc);
    return staging;
}

AssetCommitResult MaterialAssetLoader::Commit(AssetStaging&& staged)
{
    return { CommitTyped(std::move(staged)).IsValid() };
}

MaterialHandle MaterialAssetLoader::CommitTyped(AssetStaging&& staged)
{
    if (!staged.IsValid())
    {
        Log.Error("MaterialAssetLoader: commit of failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return {};
    }

    const MaterialDescription* desc = std::any_cast<MaterialDescription>(&staged.Payload);
    if (desc == nullptr)
    {
        Log.Error("MaterialAssetLoader: staging payload for '{}' is not a MaterialDescription",
                  staged.Record.Path);
        return {};
    }

    if (!Materials)
    {
        Log.Error("MaterialAssetLoader: missing MaterialCache for '{}'", staged.Record.Path);
        return {};
    }

    Material material;
    material.Pass = ShaderPassId::ForwardOpaque;
    material.BaseColor = desc->BaseColorFactor;
    material.EmissiveFactor = desc->EmissiveFactor;
    material.NormalScale = desc->NormalScale;
    material.RoughnessFactor = desc->RoughnessFactor;
    material.MetallicFactor = desc->MetallicFactor;
    material.AlphaMode = desc->AlphaMode;
    material.AlphaCutoff = desc->AlphaCutoff;

    if (material.AlphaMode == MaterialAlphaMode::Blend)
    {
        Log.Warn("MaterialAssetLoader: material '{}' uses alpha_mode 'blend'; "
                 "transparent phase not implemented, rendering opaque",
                 staged.Record.Path);
    }

    std::vector<TextureCacheHandle> ownedTextures;
    ResolveTextureSlot(desc->BaseColorTexture, /*srgb*/ true,
                       material.BaseColorTextureIndex, ownedTextures);
    ResolveTextureSlot(desc->NormalTexture, /*srgb*/ false,
                       material.NormalTextureIndex, ownedTextures);
    ResolveTextureSlot(desc->OrmTexture, /*srgb*/ false,
                       material.OrmTextureIndex, ownedTextures);
    ResolveTextureSlot(desc->EmissiveTexture, /*srgb*/ true,
                       material.EmissiveTextureIndex, ownedTextures);

    MaterialHandle handle =
        Materials->Register(staged.Record.Path, material, std::move(ownedTextures));
    if (!handle.IsValid())
        Log.Error("MaterialAssetLoader: failed to create material runtime resource: {}",
                  staged.Record.Path);

    return handle;
}

void MaterialAssetLoader::ResolveTextureSlot(const AssetRef& ref,
                                             bool srgb,
                                             uint32_t& outIndex,
                                             std::vector<TextureCacheHandle>& owned)
{
    if (!ref.IsValid())
        return;

    if (!Textures)
    {
        Log.Warn("MaterialAssetLoader: no TextureCache; texture '{}' left at neutral default",
                 ref.Path);
        return;
    }

    TextureHandle handle = Assets.LoadTexture(ref.Path, srgb);
    if (!handle.IsValid())
    {
        Log.Error("MaterialAssetLoader: failed to resolve texture '{}'; using neutral default",
                  ref.Path);
        return;
    }

    const BindlessImageIndex bindless = Textures->GetBindlessIndex(handle);
    if (!bindless.IsValid())
    {
        Log.Error("MaterialAssetLoader: texture '{}' has no bindless slot; using neutral default",
                  ref.Path);
        Textures->Release(handle);
        return;
    }

    outIndex = bindless.Value;
    // LoadTexture already incremented the refcount -- wrap without attaching.
    owned.emplace_back(Textures, handle, TextureCacheHandle::NoAttachTag{});
}
