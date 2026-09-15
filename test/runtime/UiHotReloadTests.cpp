#include <gtest/gtest.h>

#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageCache.h>
#include <assets/ui/UiPackageSerializer.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

// Editing a document while it is open.
//
// The thing that makes the loop worth having is not that the document rebuilds
// -- it is what survives the rebuild. An author tweaking a stylesheet must not
// watch the HUD forget the health it was showing, or the list forget its rows,
// or the host have to re-describe a screen it already described.

namespace
{
std::vector<std::byte> BytesOf(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

class TempAssetRoot
{
public:
    TempAssetRoot()
    {
        std::random_device rd;
        Root = std::filesystem::temp_directory_path()
             / ("sencha_ui_reload_test_" + std::to_string(rd()));
        std::filesystem::create_directories(Root);
    }
    ~TempAssetRoot()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
    TempAssetRoot(const TempAssetRoot&) = delete;
    TempAssetRoot& operator=(const TempAssetRoot&) = delete;

    void WriteBytes(std::string_view relPath, std::span<const std::byte> bytes) const
    {
        const std::filesystem::path full = Root / relPath;
        std::filesystem::create_directories(full.parent_path());
        std::ofstream file(full, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    [[nodiscard]] std::string PathString() const { return Root.generic_string(); }

private:
    std::filesystem::path Root;
};

constexpr std::string_view kMarkup = R"(<rml>
<head><link type="text/rcss" href="hud.rcss"/></head>
<body data-model="hud">
    <div id="bar" data-style-width="health + 'px'"/>
    <div id="rows"><div class="row" data-for="item : items"/></div>
</body>
</rml>)";

UiPackage MakePackage(std::string_view style)
{
    UiPackage package;
    package.RootDocumentName = "hud.rml";

    UiPackageBlob root;
    root.VirtualName = "hud.rml";
    root.SourcePath = "ui/hud.rml";
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(kMarkup);
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = "hud.rcss";
    sheet.SourcePath = "ui/hud.rcss";
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(style);
    package.Blobs.push_back(std::move(sheet));
    return package;
}

// The row height is what the edit changes, so a measurement can tell the old
// stylesheet from the new one.
std::string StyleWithRowHeight(int rowHeight)
{
    return "body { display: block; width: 100%; height: 100%; }\n"
           "#bar { display: block; height: 10px; }\n"
           "#rows { display: block; width: 400px; }\n"
           ".row { display: block; height: " + std::to_string(rowHeight) + "px; }\n";
}

UiScreenDesc MakeDesc()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://ui/hud.sui";
    desc.ModelName = "hud";
    desc.Properties = { UiModelProperty{ "health", UiValue(100.0) } };
    desc.Arrays = { "items" };
    return desc;
}

class Harness
{
public:
    Harness()
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        std::vector<std::byte> bytes;
        EXPECT_TRUE(WriteSuiToBytes(MakePackage(StyleWithRowHeight(20)), bytes));
        Root.WriteBytes("ui/hud.sui", bytes);

        ScanAssetsDirectory(Root.PathString(), Assets.Registry, Assets.Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages,
                                         Assets.Fonts, nullptr, nullptr);
    }
    ~Harness() { if (Ui != nullptr) Ui->Shutdown(); }

    UiService& Service() { return *Ui; }

    // Stands in for the asset hot reloader: what it does at the drain point is
    // exactly this, with bytes that came from a re-cook.
    [[nodiscard]] bool ReloadWith(const UiPackage& package)
    {
        return Assets.UiPackages.ReloadInPlace("asset://ui/hud.sui", package);
    }

private:
    TempAssetRoot Root;
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
};
} // namespace

TEST(UiHotReload, AnEditedStylesheetRebuildsTheOpenDocument)
{
    Harness harness;
    UiService& ui = harness.Service();
    ASSERT_TRUE(ui.IsReady());

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());

    const std::vector<std::string> items = { "a", "b", "c" };
    EXPECT_TRUE(ui.SetArray(screen, UiArrayIdAt(0), items));
    ui.Update();
    ASSERT_TRUE(ui.MeasureElement(screen, "rows").has_value());
    EXPECT_FLOAT_EQ(ui.MeasureElement(screen, "rows")->Height, 60.0f);

    // The author changes a row's height.
    ASSERT_TRUE(harness.ReloadWith(MakePackage(StyleWithRowHeight(30))));
    ui.Update();

    EXPECT_FLOAT_EQ(ui.MeasureElement(screen, "rows")->Height, 90.0f)
        << "the edit did not reach the open document";
}

TEST(UiHotReload, PublishedStateSurvivesTheRebuild)
{
    // The whole reason the loop is usable: an author tweaking a stylesheet must
    // not watch the screen forget what the host told it to show.
    Harness harness;
    UiService& ui = harness.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());

    EXPECT_TRUE(ui.SetValue(screen, UiPropertyIdAt(0), UiValue(42.0)));
    const std::vector<std::string> items = { "a", "b", "c" };
    EXPECT_TRUE(ui.SetArray(screen, UiArrayIdAt(0), items));
    ui.Update();
    ASSERT_FLOAT_EQ(ui.MeasureElement(screen, "bar")->Width, 42.0f);

    ASSERT_TRUE(harness.ReloadWith(MakePackage(StyleWithRowHeight(30))));
    ui.Update();

    // The value and the list both came through, and the binding that reads them
    // was rebuilt against them.
    EXPECT_FLOAT_EQ(ui.GetValue(screen, UiPropertyIdAt(0)).AsFloat(), 42.0);
    EXPECT_EQ(ui.ArraySize(screen, UiArrayIdAt(0)), 3u);
    EXPECT_FLOAT_EQ(ui.MeasureElement(screen, "bar")->Width, 42.0f)
        << "the rebuilt document is not reading the value the host published";
    EXPECT_FLOAT_EQ(ui.MeasureElement(screen, "rows")->Height, 90.0f);
}

TEST(UiHotReload, TheScreenHandleAndItsIdsSurvive)
{
    // A host resolved its ids once at open. A reload that renumbered them would
    // make every SetValue afterwards address the wrong property.
    Harness harness;
    UiService& ui = harness.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());

    const UiModelPropertyId health = ui.FindProperty(screen, "health");
    const UiModelArrayId list = ui.FindArray(screen, "items");
    ASSERT_TRUE(health.IsValid());
    ASSERT_TRUE(list.IsValid());

    ASSERT_TRUE(harness.ReloadWith(MakePackage(StyleWithRowHeight(30))));
    ui.Update();

    EXPECT_TRUE(ui.IsScreenOpen(screen)) << "the reload invalidated the host's handle";
    EXPECT_EQ(ui.FindProperty(screen, "health"), health);
    EXPECT_EQ(ui.FindArray(screen, "items"), list);
    EXPECT_TRUE(ui.SetValue(screen, health, UiValue(7.0)));
}

TEST(UiHotReload, AnUnchangedPackageRebuildsNothing)
{
    // Reconstruction is not free -- it drops focus and any transient edit. It
    // must happen when the package changed, and only then.
    Harness harness;
    UiService& ui = harness.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());
    ui.Update();

    const float before = ui.MeasureElement(screen, "rows")->Height;
    for (int i = 0; i < 5; ++i)
        ui.Update();

    EXPECT_FLOAT_EQ(ui.MeasureElement(screen, "rows")->Height, before);
    EXPECT_TRUE(ui.IsScreenOpen(screen));
}

TEST(UiHotReload, ReloadingAPackageNobodyHasOpenIsNotAnError)
{
    Harness harness;
    UiService& ui = harness.Service();
    ASSERT_TRUE(ui.IsReady());

    // Not resident: nothing has leased it, so there is nothing to swap.
    EXPECT_FALSE(harness.ReloadWith(MakePackage(StyleWithRowHeight(30))));
}

TEST(UiHotReload, AnInvalidPackageIsRefusedAndTheOpenDocumentIsLeftAlone)
{
    Harness harness;
    UiService& ui = harness.Service();

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, MakeDesc());
    ASSERT_TRUE(screen.IsValid());
    ui.Update();
    const float before = ui.MeasureElement(screen, "rows")->Height;

    EXPECT_FALSE(harness.ReloadWith(UiPackage{}))
        << "an invalid package was swapped into the cache";
    ui.Update();

    EXPECT_TRUE(ui.IsScreenOpen(screen));
    EXPECT_FLOAT_EQ(ui.MeasureElement(screen, "rows")->Height, before)
        << "a refused reload disturbed the document that was already open";
}
