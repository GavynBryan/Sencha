#pragma once

#include <imgui.h>

#include <cstdint>

// Where an icon or an ornament comes from. Every id has a table row holding
// each form it can take; the preferred form is drawn, and the others are the
// fallback chain (sprite, then procedural, then font glyph) so a look stays
// complete when an authored sprite is missing or has not been made yet.
// Registering authored art means replacing a row, not redesigning a widget.
namespace EditorChrome
{
enum class GlyphSourceKind : std::uint8_t
{
    Procedural, // drawn from strokes at draw time
    FontGlyph,  // a glyph merged into the UI font
    Sprite,     // a region of a texture: a standalone sprite or an atlas cell
};

struct SpriteRef
{
    ImTextureID Texture = 0;
    ImVec2 Uv0{ 0.0f, 0.0f };
    ImVec2 Uv1{ 1.0f, 1.0f };
    [[nodiscard]] bool Valid() const { return Texture != 0; }
};

// Draws the glyph into [mn, mx] (a square for icons) in `tint`.
using ProceduralGlyphFn = void (*)(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint);

struct IconSource
{
    GlyphSourceKind Preferred = GlyphSourceKind::Procedural;
    ProceduralGlyphFn Procedural = nullptr;
    const char* FontGlyph = nullptr; // UTF-8, an ICON_FA_* literal
    SpriteRef Sprite{};
};

struct OrnamentSource
{
    GlyphSourceKind Preferred = GlyphSourceKind::Procedural;
    ProceduralGlyphFn Procedural = nullptr;
    SpriteRef Sprite{};
};
} // namespace EditorChrome
