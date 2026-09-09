#include "ui/chrome/ChromeOrnaments.h"
#include "ui/chrome/IconDraw.h"

#include <gtest/gtest.h>

using namespace EditorChrome;

TEST(ChromeSources, EveryIconHasAFontGlyphFallback)
{
    // A control never goes blank: whatever form is preferred, the glyph
    // merged into the UI font is always there behind it.
    for (std::size_t i = 1; i < static_cast<std::size_t>(IconId::Count); ++i)
    {
        const IconSource& source = IconSourceFor(static_cast<IconId>(i));
        EXPECT_NE(source.FontGlyph, nullptr) << "icon " << i;
        EXPECT_NE(source.Procedural, nullptr) << "icon " << i;
        EXPECT_EQ(source.Preferred, GlyphSourceKind::Procedural) << "icon " << i;
    }
    EXPECT_EQ(IconSourceFor(IconId::None).Procedural, nullptr);
    EXPECT_EQ(IconSourceFor(IconId::None).FontGlyph, nullptr);
}

TEST(ChromeSources, SpriteOverrideIsReflectedAndReset)
{
    const IconSource before = IconSourceFor(IconId::Box);
    IconSource authored = before;
    authored.Preferred = GlyphSourceKind::Sprite;
    authored.Sprite = SpriteRef{ .Texture = 1, .Uv0 = ImVec2(0.25f, 0.0f), .Uv1 = ImVec2(0.5f, 0.25f) };
    SetIconSource(IconId::Box, authored);

    const IconSource& now = IconSourceFor(IconId::Box);
    EXPECT_EQ(now.Preferred, GlyphSourceKind::Sprite);
    EXPECT_TRUE(now.Sprite.Valid());
    EXPECT_FLOAT_EQ(now.Sprite.Uv1.x, 0.5f);
    // The procedural and glyph forms stay on the row as the fallback chain.
    EXPECT_EQ(now.Procedural, before.Procedural);
    EXPECT_EQ(now.FontGlyph, before.FontGlyph);

    ResetIconSources();
    EXPECT_EQ(IconSourceFor(IconId::Box).Preferred, GlyphSourceKind::Procedural);
    EXPECT_FALSE(IconSourceFor(IconId::Box).Sprite.Valid());
}

TEST(ChromeSources, OutOfRangeIdsResolveToNone)
{
    const IconSource& source = IconSourceFor(IconId::Count);
    EXPECT_EQ(source.Procedural, nullptr);
    SetIconSource(IconId::Count, IconSource{ GlyphSourceKind::Sprite, nullptr, "x", {} });
    EXPECT_EQ(IconSourceFor(IconId::None).FontGlyph, nullptr);
}

TEST(ChromeSources, EveryOrnamentDrawsProcedurallyAndTakesASprite)
{
    for (std::size_t i = 0; i <= static_cast<std::size_t>(OrnamentKind::Scanlines); ++i)
    {
        const OrnamentSource& source = OrnamentSourceFor(static_cast<OrnamentKind>(i));
        EXPECT_NE(source.Procedural, nullptr) << "ornament " << i;
        EXPECT_EQ(source.Preferred, GlyphSourceKind::Procedural) << "ornament " << i;
    }

    OrnamentSource authored = OrnamentSourceFor(OrnamentKind::Vent);
    authored.Preferred = GlyphSourceKind::Sprite;
    authored.Sprite = SpriteRef{ .Texture = 7 };
    SetOrnamentSource(OrnamentKind::Vent, authored);
    EXPECT_EQ(OrnamentSourceFor(OrnamentKind::Vent).Preferred, GlyphSourceKind::Sprite);
    EXPECT_NE(OrnamentSourceFor(OrnamentKind::Vent).Procedural, nullptr);
    ResetOrnamentSources();
    EXPECT_EQ(OrnamentSourceFor(OrnamentKind::Vent).Preferred, GlyphSourceKind::Procedural);
}
