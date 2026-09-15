#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

//=============================================================================
// FontFace
//
// One typeface as the document engine wants it: the raw face bytes plus the
// family, style and weight it should be registered under.
//
// Bytes, not a path. The font engine is handed a span and never a filename, so
// a shipped build has no production filesystem dependency for text and a face
// can come from a pack file the day one exists.
//=============================================================================

enum class FontStyle : std::uint16_t
{
    Normal = 0,
    Italic = 1,
};

// The CSS numeric weight scale. Kept as a number rather than an enum of names
// because that is what the scale is, and a face may sit anywhere on it.
inline constexpr std::uint16_t kFontWeightNormal = 400;
inline constexpr std::uint16_t kFontWeightBold = 700;

struct FontFace
{
    // What documents name in `font-family`. Defaulted from the source filename
    // at cook time, overridable in the sidecar.
    std::string Family;
    FontStyle Style = FontStyle::Normal;
    std::uint16_t Weight = kFontWeightNormal;

    // Whether this face is a fallback: consulted only for glyphs no other face
    // in the family provides. A CJK or symbol face is registered this way.
    bool Fallback = false;

    std::vector<std::byte> Bytes;

    [[nodiscard]] bool IsValid() const { return !Family.empty() && !Bytes.empty(); }
};
