#include <assets/runtime/AssetStandIns.h>

#include <assets/texture/TextureCache.h>
#include <render/MaterialCache.h>

#include <utility>
#include <vector>

TextureData StandInTexture()
{
    constexpr std::uint8_t kPink[4] = { 255, 0, 255, 255 };
    constexpr std::uint8_t kBlack[4] = { 0, 0, 0, 255 };

    TextureData texture;
    texture.Format = TexturePixelFormat::RGBA8_SRGB;
    texture.Usage = TextureUsage::BaseColor;
    // Point-sampled, so four texels are four sharp squares at any distance.
    texture.Filter = TextureFilter::Nearest;
    texture.Width = 2;
    texture.Height = 2;
    for (const std::uint8_t* texel : { kPink, kBlack, kBlack, kPink })
        texture.Blob.insert(texture.Blob.end(), texel, texel + 4);
    texture.Mips.push_back({ .Width = 2, .Height = 2, .Offset = 0, .ByteSize = texture.Blob.size() });
    return texture;
}

Material StandInMaterial(std::uint32_t checkerTextureIndex)
{
    Material material;
    material.Shading = MaterialShading::Unlit;
    material.BaseColor = checkerTextureIndex == UINT32_MAX ? Vec4(1.0f, 0.0f, 1.0f, 1.0f) : Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    material.BaseColorTextureIndex = checkerTextureIndex;
    material.DoubleSided = true;
    return material;
}

AssetLease PutStandInTexture(TextureCache& textures, std::string_view path)
{
    const TextureData texture = StandInTexture();
    const TextureHandle handle =
        textures.CreateFromTextureData(path, texture, TextureCache::SamplerForTextureData(texture));
    return handle.IsValid() ? AssetLease::Adopt(AssetType::Texture, textures, handle.ToToken()) : AssetLease{};
}

AssetLease PutStandInMaterial(MaterialCache& materials, TextureCache* textures, std::string_view path)
{
    std::vector<TextureCacheHandle> owned;
    std::uint32_t checkerIndex = UINT32_MAX;
    if (textures != nullptr)
    {
        const TextureData texture = StandInTexture();
        const TextureHandle checker =
            textures->CreateFromTextureData({}, texture, TextureCache::SamplerForTextureData(texture));
        if (checker.IsValid())
        {
            checkerIndex = textures->GetBindlessIndex(checker).Value;
            owned.emplace_back(textures, checker, TextureCacheHandle::NoAttachTag{});
        }
    }
    const MaterialHandle handle = materials.Register(path, StandInMaterial(checkerIndex), std::move(owned));
    return handle.IsValid() ? AssetLease::Adopt(AssetType::Material, materials, handle.ToToken()) : AssetLease{};
}
