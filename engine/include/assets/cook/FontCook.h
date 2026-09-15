#pragma once

#include <assets/cook/AssetImporter.h>

//=============================================================================
// Font cook. Dev-only -- compiled under SENCHA_ENABLE_COOK, never shipped.
//
// A .ttf/.otf source becomes a .sfont: the face bytes passed through unchanged,
// plus the family, style and weight it should be registered under. Nothing is
// rasterised here. A glyph atlas depends on the size a document asks for and on
// the renderer that will sample it, so it belongs to the runtime that draws the
// text, not to the cook -- baking one here would fix a size at author time and
// then be wrong at every other.
//
// Family, style and weight default from the filename ("Inter-BoldItalic.ttf" ->
// family "Inter", weight 700, italic) and are overridden by the source's .meta
// sidecar. Guessing is the convenience; the sidecar is the answer when the
// guess is wrong.
//=============================================================================
class FontFaceImporter final : public IAssetImporter
{
public:
    [[nodiscard]] std::vector<std::string_view> SourceExtensions() const override;
    [[nodiscard]] ImportResult Import(const ImportInput& input,
                                      ICookOutputWriter& output) override;
};
