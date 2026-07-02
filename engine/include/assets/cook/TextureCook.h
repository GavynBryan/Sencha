#pragma once

#include <assets/cook/AssetImporter.h>
#include <render/TextureData.h>

#include <string>
#include <string_view>

struct Image;
class LoggingProvider;

//=============================================================================
// Texture cook (docs/assets/pipeline.md, Decisions B, E, L). Dev-only —
// compiled under SENCHA_ENABLE_COOK, never shipped.
//
// The cook step owns mip generation: the runtime never generates mips.
// Downsampling is colorspace-correct (linearize → filter → re-encode for
// sRGB usages) and normal maps are renormalized per level.
//
// Cooked output is BC-compressed per the Decision L table (bc7enc/rgbcx,
// cook-only dependencies): BaseColor/Emissive -> BC7_SRGB, Normal -> BC5
// (X/Y in the two channels, Z reconstructed in-shader), Orm -> BC7.
// LinearData also cooks to BC7 for now: PNG decode always yields RGBA, so
// the "BC4 by channel count" half of the table waits for a source format
// that actually carries channel count.
//=============================================================================

// Usage from the source file's stem suffix, the cook-side authoring
// convention: "_n"/"_nrm"/"_normal" → Normal, "_orm" → Orm,
// "_em"/"_emissive" → Emissive, "_mask"/"_lin"/"_data" → LinearData,
// anything else → BaseColor.
[[nodiscard]] TextureUsage InferTextureUsageFromName(std::string_view sourceRelPath);

// The Decision L format table: which cooked format a usage compresses to.
[[nodiscard]] TexturePixelFormat CookedFormatForUsage(TextureUsage usage);

// Decoded image + usage → full RGBA8 mip chain (colorspace-correct
// filtering, normal renormalization), uncompressed. The filtering stage of
// the cook, exposed so its correctness is testable on readable pixels.
// Pure; errors travel in `error`.
[[nodiscard]] bool BuildTextureMipChainRgba8(const Image& image,
                                             TextureUsage usage,
                                             TextureData& out,
                                             std::string* error = nullptr);

// Decoded image + usage → full mip chain TextureData, BC-compressed per
// CookedFormatForUsage. Pure; errors travel in `error`.
[[nodiscard]] bool CookImageToTexture(const Image& image,
                                      TextureUsage usage,
                                      TextureData& out,
                                      std::string* error = nullptr);

// The full-control form behind the usage-only overload, driven by the
// source's import settings. Compress=false keeps the colorspace-correct
// RGBA8/RGBA8_SRGB chain uncompressed (pixel art: BC blocks smear crisp
// texels); GenerateMips=false emits mip 0 only; Filter rides into the
// TextureData so the runtime samples per the author's choice.
struct TextureCookParams
{
    TextureUsage Usage = TextureUsage::BaseColor;
    TextureFilter Filter = TextureFilter::Linear;
    bool Compress = true;
    bool GenerateMips = true;
};
[[nodiscard]] bool CookImageToTexture(const Image& image,
                                      const TextureCookParams& params,
                                      TextureData& out,
                                      std::string* error = nullptr);

//=============================================================================
// PngTextureImporter — .png → cooked .stex.
//
// The artifact keeps the source's virtual path ("asset://.../checker.png"
// serves cooked .stex bytes): authored references never churn when the
// cooked format evolves. The physical artifact lives at
// ".cooked/<source-path>.stex".
//=============================================================================
class PngTextureImporter final : public IAssetImporter
{
public:
    [[nodiscard]] std::vector<std::string_view> SourceExtensions() const override;
    [[nodiscard]] ImportResult Import(const ImportInput& input,
                                      ICookOutputWriter& output) override;
};
