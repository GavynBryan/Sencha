#pragma once

#include <assets/cook/AssetImporter.h> // kImportSettingsSuffix
#include <assets/font/FontFace.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

//=============================================================================
// FontImportSettings. Dev-only, compiled under SENCHA_ENABLE_COOK.
//
// Per-source overrides for what a face is registered as, authored as a JSON
// sidecar next to the source ("Inter-Bold.ttf" + "Inter-Bold.ttf.meta"). The
// import driver reads the sidecar and hands its bytes to the importer, and the
// sidecar participates in the cooked-cache freshness hash so editing it
// recooks.
//
// Every field is optional because the filename convention usually answers it:
// "Inter-BoldItalic.ttf" is family "Inter", weight 700, italic. The sidecar is
// what you reach for when the convention is wrong or absent, which is why an
// unparsable one is an error rather than a fall back to the guess -- a sidecar
// exists because somebody meant something specific.
//
// Schema (all fields optional):
//   {
//     "version": 1,
//     "family": "Inter",
//     "weight": 400,                     // CSS scale, 1-1000
//     "style": "normal" | "italic",
//     "fallback": true | false           // consulted only for missing glyphs
//   }
//=============================================================================

struct FontImportSettings
{
    // Unset = take it from the filename convention.
    std::optional<std::string> Family;
    std::optional<std::uint16_t> Weight;
    std::optional<FontStyle> Style;
    std::optional<bool> Fallback;
};

// Parses sidecar JSON bytes. Empty input yields all-unset (a missing sidecar is
// not an error); malformed JSON, an out-of-range weight, or an unknown style
// fails with *error so a typo cannot silently cook with a guess.
[[nodiscard]] bool ParseFontImportSettings(std::span<const std::byte> bytes,
                                           FontImportSettings& out,
                                           std::string* error = nullptr);

// The family, weight and style a filename implies: "Inter-BoldItalic" ->
// ("Inter", 700, Italic). An unrecognised suffix is left as part of the family
// rather than dropped, because "Inter-Display" is a different family and not a
// weight. Shared with tests, which is most of why it is exposed.
void InferFontFaceFromFileName(std::string_view sourceRelPath,
                               std::string& outFamily,
                               std::uint16_t& outWeight,
                               FontStyle& outStyle);

[[nodiscard]] std::string_view FontStyleName(FontStyle style);
[[nodiscard]] bool FontStyleFromName(std::string_view name, FontStyle& out);
