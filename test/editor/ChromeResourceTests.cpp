// The resource architecture, not the appearance: what a theme change is
// allowed to invalidate. Style values (colors, metrics, decor, finishes) must
// cost nothing; theme artwork has its own lifetime; the font atlas is rebuilt
// only when fonts, icons or the mark actually change.
#include "ui/EditorThemeFile.h"
#include "ui/EditorUiStyle.h"
#include "ui/ThemeTextureCache.h"
#include "ui/chrome/ChromeFrame.h"
#include "ui/chrome/ChromeGeometry.h"
#include "ui/chrome/IconDraw.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
EditorChrome::ShellAtlasKey CurrentAtlasKey()
{
    return EditorChrome::ShellAtlasKey{ .UiScale = EditorUi::UiScale, .LogoPath = "branding/kyusu-logo.png" };
}

std::filesystem::path WriteTemp(const char* name, const char* bytes)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream file(path, std::ios::binary);
    file << bytes;
    return path;
}
}

TEST(ShellAtlasKey, ThemeStateIsNotAnAtlasInput)
{
    const EditorChrome::ShellAtlasKey before = CurrentAtlasKey();

    // Everything a theme can change, changed.
    EditorUi::Accent = ImVec4(1.0f, 0.0f, 0.5f, 1.0f);
    EditorUi::MetalBase = ImVec4(0.4f, 0.3f, 0.2f, 1.0f);
    EditorUi::Metrics.Chamfer = 19.0f;
    EditorUi::Metrics.BarClearance = 11.0f;   // caption geometry: the tempting one
    EditorUi::Metrics.HeaderHeight = 44.0f;
    EditorUi::Decor.HierarchyEmpty = "ANYTHING";
    EditorUi::Surfaces.Caption = EditorUi::BarFinish::Texture;
    EditorUi::Surfaces.CaptionTexture = "wood.png";
    EditorUi::Surfaces.CaptionModulate = EditorUi::SurfaceModulation::Metal;

    // None of it reaches the font atlas. If this ever fails, something made a
    // theme switch rebuild the fonts.
    EXPECT_EQ(CurrentAtlasKey(), before);

    ResetEditorTheme();
    EXPECT_EQ(CurrentAtlasKey(), before);
}

TEST(ShellAtlasKey, ScaleAndMarkAreAtlasInputs)
{
    EditorChrome::ShellAtlasKey key;
    EditorChrome::ShellAtlasKey scaled = key;
    scaled.UiScale = 1.5f;
    EXPECT_FALSE(scaled == key);

    EditorChrome::ShellAtlasKey marked = key;
    marked.LogoPath = "other.png";
    EXPECT_FALSE(marked == key);
}

TEST(SurfaceTextures, OnlyATexturedFinishAsksForAnAsset)
{
    EditorUi::ChromeSurfaces surfaces;
    std::vector<std::string> requested;

    surfaces.Caption = EditorUi::BarFinish::GradientX;
    surfaces.CaptionTexture = "unused.png"; // named but not selected
    surfaces.Toolbar = EditorUi::BarFinish::Solid;
    EditorUi::RequestedSurfaceTextures(surfaces, requested);
    EXPECT_TRUE(requested.empty());

    surfaces.Caption = EditorUi::BarFinish::Texture;
    surfaces.CaptionTexture = "a.png";
    EditorUi::RequestedSurfaceTextures(surfaces, requested);
    ASSERT_EQ(requested.size(), 1u);
    EXPECT_EQ(requested[0], "a.png");

    // Switching the asset asks for exactly the new one.
    surfaces.CaptionTexture = "b.png";
    EditorUi::RequestedSurfaceTextures(surfaces, requested);
    ASSERT_EQ(requested.size(), 1u);
    EXPECT_EQ(requested[0], "b.png");

    // Two bars naming the same asset ask for it once.
    surfaces.Toolbar = EditorUi::BarFinish::Texture;
    surfaces.ToolbarTexture = "b.png";
    EditorUi::RequestedSurfaceTextures(surfaces, requested);
    EXPECT_EQ(requested.size(), 1u);
}

TEST(ThemeTextureSource, IdentityIsThePathAndItsContentStamp)
{
    const auto path = WriteTemp("sencha_theme_source_test.bin", "first");
    const std::string name = path.string();

    const ThemeTextureCache::SourceStamp first = ThemeTextureCache::StampOf(name);
    EXPECT_TRUE(first.Exists);
    EXPECT_EQ(first, ThemeTextureCache::StampOf(name)); // unchanged file, unchanged stamp

    // Edited in place: the path is the same and the resource must not be.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    { std::ofstream rewrite(path, std::ios::binary); rewrite << "second, and longer"; }
    const ThemeTextureCache::SourceStamp edited = ThemeTextureCache::StampOf(name);
    EXPECT_TRUE(edited.Exists);
    EXPECT_FALSE(edited == first);

    // Absent is a stamp of its own, so a file that appears makes a negative
    // entry stale instead of leaving it cached as broken.
    std::filesystem::remove(path);
    const ThemeTextureCache::SourceStamp missing = ThemeTextureCache::StampOf(name);
    EXPECT_FALSE(missing.Exists);
    EXPECT_FALSE(missing == edited);

    { std::ofstream recreate(path, std::ios::binary); recreate << "repaired"; }
    const ThemeTextureCache::SourceStamp repaired = ThemeTextureCache::StampOf(name);
    EXPECT_TRUE(repaired.Exists);
    EXPECT_FALSE(repaired == missing);
    std::filesystem::remove(path);
}

TEST(ChromeSpec, ThePrimaryViewportIsTheHeavyComposition)
{
    const EditorChrome::PanelChromeSpec primary = EditorChrome::ChromeSpecFor(PanelStyle::ViewportPrimary);
    const EditorChrome::PanelChromeSpec plain = EditorChrome::ChromeSpecFor(PanelStyle::Viewport);

    EXPECT_GT(primary.Frame.Chamfer, plain.Frame.Chamfer);
    EXPECT_GT(primary.Frame.Border + primary.Frame.Recess, plain.Frame.Border + plain.Frame.Recess);
    EXPECT_GT(primary.HeaderHeight, plain.HeaderHeight);
    EXPECT_EQ(primary.Mount, EditorChrome::OrnamentMount::Ring);
    EXPECT_EQ(primary.Header, EditorChrome::HeaderPlate::Bezel);
    EXPECT_TRUE(primary.CornerBrackets);

    // The heavier ring costs the scene that much area and no more: the padding
    // inside the ring stays as tight as the quiet composition's, so the bezel
    // is frame rather than margin.
    const float primaryRing = primary.Frame.Border + primary.Frame.Recess;
    const float plainRing = plain.Frame.Border + plain.Frame.Recess;
    EXPECT_FLOAT_EQ(primary.ContentPadding.x - primaryRing, plain.ContentPadding.x - plainRing);
    EXPECT_FLOAT_EQ(primary.ContentPadding.y - primaryRing, plain.ContentPadding.y - plainRing);

    // Every other composition mounts in the well and carries a plain plate, so
    // nothing else changed underneath.
    for (const PanelStyle style : { PanelStyle::Standard, PanelStyle::Tool, PanelStyle::Viewport, PanelStyle::Compact })
    {
        const EditorChrome::PanelChromeSpec spec = EditorChrome::ChromeSpecFor(style);
        EXPECT_EQ(spec.Mount, EditorChrome::OrnamentMount::Well);
        EXPECT_EQ(spec.Header, EditorChrome::HeaderPlate::Plain);
        EXPECT_FALSE(spec.CornerBrackets);
    }
}
