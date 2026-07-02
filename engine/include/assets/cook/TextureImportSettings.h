#pragma once

#include <assets/cook/AssetImporter.h> // kImportSettingsSuffix
#include <render/TextureData.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

//=============================================================================
// TextureImportSettings. Dev-only, compiled under SENCHA_ENABLE_COOK.
//
// Per-source import options, authored as a JSON sidecar next to the source
// ("checker.png" + "checker.png.meta"). The import driver reads the sidecar
// and hands its bytes to the importer (importers stay filesystem-free), and
// the sidecar participates in the cooked-cache freshness hash so editing it
// recooks. A missing sidecar is the defaults: today's behavior exactly.
//
// Schema (all fields optional):
//   {
//     "version": 1,
//     "usage": "base_color" | "normal" | "orm" | "emissive" | "linear_data",
//     "filter": "linear" | "nearest",
//     "compress": true | false,
//     "mips": true | false
//   }
//=============================================================================

struct TextureImportSettings
{
    // Unknown = infer from the filename suffix convention
    // (InferTextureUsageFromName).
    TextureUsage Usage = TextureUsage::Unknown;
    TextureFilter Filter = TextureFilter::Linear;
    // false = uncompressed RGBA8/RGBA8_SRGB (pixel art: BC blocks smear
    // crisp texels, which point sampling then faithfully magnifies).
    bool Compress = true;
    bool GenerateMips = true;

    bool operator==(const TextureImportSettings&) const = default;
};

// Parses sidecar JSON bytes. Empty input yields the defaults (a missing
// sidecar is not an error); malformed JSON or unknown values fail with
// *error so a typo cannot silently cook with defaults.
[[nodiscard]] bool ParseTextureImportSettings(std::span<const std::byte> bytes,
                                              TextureImportSettings& out,
                                              std::string* error = nullptr);

// Writes the sidecar next to the source (pretty JSON, all fields explicit).
[[nodiscard]] bool SaveTextureImportSettingsFile(std::string_view path,
                                                 const TextureImportSettings& settings,
                                                 std::string* error = nullptr);

// Name helpers shared by the JSON forms and editor UI.
[[nodiscard]] std::string_view TextureUsageName(TextureUsage usage);
[[nodiscard]] bool TextureUsageFromName(std::string_view name, TextureUsage& out);
