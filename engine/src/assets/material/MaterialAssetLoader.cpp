#include <assets/material/MaterialAssetLoader.h>

#include <assets/material/MaterialLoader.h>
#include <assets/runtime/AssetSystem.h>
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

    // CommitTyped resolves each slot through the front door, so every texture
    // it names has to be resident by then. Declaring them here is what lets
    // the async driver order the commits instead of this loader loading them
    // inline on the owner thread.
    for (const AssetRef* reference : { &desc.BaseColorTexture, &desc.NormalTexture,
                                       &desc.OrmTexture, &desc.EmissiveTexture })
    {
        if (reference->IsValid())
            staging.Dependencies.push_back(*reference);
    }

    staging.Payload = std::move(desc);
    return staging;
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

    std::vector<TextureCacheHandle> ownedTextures;
    Material material = ResolveDescription(*desc, staged.Record.Path, ownedTextures);

    MaterialHandle handle =
        Materials->Register(staged.Record.Path, material, std::move(ownedTextures));
    if (!handle.IsValid())
        Log.Error("MaterialAssetLoader: failed to create material runtime resource: {}",
                  staged.Record.Path);

    return handle;
}

bool MaterialAssetLoader::CommitReload(AssetStaging&& staged)
{
    if (!staged.IsValid())
    {
        Log.Error("MaterialAssetLoader: reload of failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return false;
    }

    const MaterialDescription* desc = std::any_cast<MaterialDescription>(&staged.Payload);
    if (desc == nullptr)
    {
        Log.Error("MaterialAssetLoader: reload payload for '{}' is not a MaterialDescription",
                  staged.Record.Path);
        return false;
    }

    if (!Materials)
    {
        Log.Error("MaterialAssetLoader: missing MaterialCache for reload of '{}'",
                  staged.Record.Path);
        return false;
    }

    std::vector<TextureCacheHandle> ownedTextures;
    Material material = ResolveDescription(*desc, staged.Record.Path, ownedTextures);

    return Materials->ReloadInPlace(staged.Record.Path, material, std::move(ownedTextures));
}

Material MaterialAssetLoader::ResolveDescription(const MaterialDescription& desc,
                                                 std::string_view,
                                                 std::vector<TextureCacheHandle>& outOwned)
{
    Material material;
    // The one classification point: a material's pass is decided at load and
    // consumers route on it, rather than each re-deriving it from AlphaMode.
    material.Pass = desc.AlphaMode == MaterialAlphaMode::Blend
        ? ShaderPassId::ForwardTransparent
        : ShaderPassId::ForwardOpaque;
    material.Shading = desc.Shading;
    material.BaseColor = desc.BaseColorFactor;
    material.EmissiveFactor = desc.EmissiveFactor;
    material.NormalScale = desc.NormalScale;
    material.RoughnessFactor = desc.RoughnessFactor;
    material.MetallicFactor = desc.MetallicFactor;
    material.SpecularIntensity = desc.SpecularIntensity;
    material.EmissiveStrength = desc.EmissiveStrength;
    material.AlphaMode = desc.AlphaMode;
    material.AlphaCutoff = desc.AlphaCutoff;
    material.DoubleSided = desc.DoubleSided;
    material.ReceiveShadows = desc.ReceiveShadows;
    material.CastShadows = desc.CastShadows;


    ResolveTextureSlot(desc.BaseColorTexture, /*srgb*/ true,
                       material.BaseColorTextureIndex, outOwned);
    ResolveTextureSlot(desc.NormalTexture, /*srgb*/ false,
                       material.NormalTextureIndex, outOwned);
    ResolveTextureSlot(desc.OrmTexture, /*srgb*/ false,
                       material.OrmTextureIndex, outOwned);
    ResolveTextureSlot(desc.EmissiveTexture, /*srgb*/ true,
                       material.EmissiveTextureIndex, outOwned);

    return material;
}

void MaterialAssetLoader::ResolveTextureSlot(const AssetRef& ref,
                                             bool srgb,
                                             uint32_t& outIndex,
                                             std::vector<TextureCacheHandle>& owned)
{
    if (!ref.IsValid())
        return;

    // No texture cache at all is a process that was composed without one -- a
    // dedicated host reading the same materials it will never draw. Every
    // material would say the same thing, and none of it is actionable.
    if (!Textures)
        return;

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
    // LoadTexture already incremented the refcount. Wrap without attaching.
    owned.emplace_back(Textures, handle, TextureCacheHandle::NoAttachTag{});
}
