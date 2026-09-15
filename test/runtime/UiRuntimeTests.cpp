#include <gtest/gtest.h>

#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackage.h>
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

// The Stage 2 proof: a cooked package reaches the runtime through the ordinary
// asset front door, becomes a document, lays out, and goes away cleanly --
// without a window, a device, or a loose .rml anywhere on disk.
//
// No graphics here on purpose. Parsing, the cascade, layout and measurement are
// device-free, and this is the test that keeps them that way.

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
             / ("sencha_ui_runtime_test_" + std::to_string(rd()));
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

// A document whose geometry is fully determined by its stylesheet, so a
// measurement can be asserted exactly rather than approximately. Deliberately
// text-free: text measurement depends on a font face, and what this proves is
// the package -> document -> layout path.
constexpr std::string_view kRootMarkup = R"(<rml>
<head>
    <link type="text/rcss" href="theme.rcss"/>
</head>
<body>
    <div id="panel">
        <div id="inner"/>
    </div>
</body>
</rml>)";

constexpr std::string_view kStyleSheet = R"(
body { display: block; width: 100%; height: 100%; }
#panel { display: block; width: 200px; height: 120px; margin-left: 40px; margin-top: 30px; }
#inner { display: block; width: 50px; height: 25px; }
)";

UiPackage MakeLayoutPackage()
{
    UiPackage package;
    package.RootDocumentName = "layout.rml";

    UiPackageBlob root;
    root.VirtualName = "layout.rml";
    root.SourcePath = "ui/layout.rml";
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(kRootMarkup);
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = "theme.rcss";
    sheet.SourcePath = "ui/theme.rcss";
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(kStyleSheet);
    package.Blobs.push_back(std::move(sheet));

    return package;
}

// Everything a device-free host needs: the asset front door, the two UI caches,
// and the service over them.
class UiTestHost
{
public:
    explicit UiTestHost(const TempAssetRoot& root)
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        ScanAssetsDirectory(root.PathString(), Assets.Registry, Assets.Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages, Assets.Fonts);
    }

    UiService& Service() { return *Ui; }

private:
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
};

void WritePackage(const TempAssetRoot& root, std::string_view relPath, const UiPackage& package)
{
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSuiToBytes(package, bytes));
    root.WriteBytes(relPath, bytes);
}
} // namespace

TEST(UiRuntimeStage2, ACookedPackageBecomesALaidOutDocument)
{
    TempAssetRoot root;
    WritePackage(root, "ui/layout.sui", MakeLayoutPackage());

    UiTestHost host(root);
    UiService& ui = host.Service();
    ASSERT_TRUE(ui.IsReady());

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    ASSERT_TRUE(surface.IsValid());

    const UiScreenHandle screen = ui.OpenScreen(surface, "asset://ui/layout.sui");
    ASSERT_TRUE(screen.IsValid()) << "the package did not open as a document";
    EXPECT_TRUE(ui.IsScreenOpen(screen));

    ui.Update();

    // The stylesheet fully determines these, so they are asserted exactly. A
    // near-miss here means the stylesheet did not reach the cascade -- which is
    // the whole point of carrying it inside the package.
    const auto panel = ui.MeasureElement(screen, "panel");
    ASSERT_TRUE(panel.has_value()) << "#panel was not found after layout";
    EXPECT_FLOAT_EQ(panel->Width, 200.0f);
    EXPECT_FLOAT_EQ(panel->Height, 120.0f);
    EXPECT_FLOAT_EQ(panel->X, 40.0f);
    EXPECT_FLOAT_EQ(panel->Y, 30.0f);

    // A nested element proves layout ran rather than a box being echoed back.
    const auto inner = ui.MeasureElement(screen, "inner");
    ASSERT_TRUE(inner.has_value());
    EXPECT_FLOAT_EQ(inner->Width, 50.0f);
    EXPECT_FLOAT_EQ(inner->X, 40.0f);
    EXPECT_FLOAT_EQ(inner->Y, 30.0f);

    EXPECT_FALSE(ui.MeasureElement(screen, "no-such-element").has_value());

    ui.CloseScreen(screen);
    EXPECT_FALSE(ui.IsScreenOpen(screen));
    EXPECT_FALSE(ui.MeasureElement(screen, "panel").has_value())
        << "a closed screen must not still answer for its document";
}

TEST(UiRuntimeStage2, AStylesheetIsServedFromThePackageAndNotFromDisk)
{
    // The same document, with the stylesheet blob removed. Nothing on disk can
    // satisfy the link, so if this still lays out at 200px the file interface
    // reached past the package.
    TempAssetRoot root;
    UiPackage package = MakeLayoutPackage();
    package.Blobs.erase(package.Blobs.begin() + 1);
    WritePackage(root, "ui/layout.sui", package);

    UiTestHost host(root);
    UiService& ui = host.Service();
    ASSERT_TRUE(ui.IsReady());

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, "asset://ui/layout.sui");
    ASSERT_TRUE(screen.IsValid());
    ui.Update();

    const auto panel = ui.MeasureElement(screen, "panel");
    ASSERT_TRUE(panel.has_value());
    EXPECT_NE(panel->Width, 200.0f)
        << "the missing stylesheet was resolved from somewhere outside the package";
}

TEST(UiRuntimeStage2, AMissingPackageIsRefusedRatherThanOpeningEmpty)
{
    TempAssetRoot root;
    UiTestHost host(root);
    UiService& ui = host.Service();
    ASSERT_TRUE(ui.IsReady());

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    EXPECT_FALSE(ui.OpenScreen(surface, "asset://ui/absent.sui").IsValid());
}

TEST(UiRuntimeStage2, LooseMarkupIsNotASecondContentPath)
{
    // A .sui whose bytes are the raw .rml a developer might drop in. Accepting
    // it would be the convenient second path that quietly becomes the real
    // development architecture.
    TempAssetRoot root;
    root.WriteBytes("ui/loose.sui", BytesOf(kRootMarkup));

    UiTestHost host(root);
    UiService& ui = host.Service();
    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    EXPECT_FALSE(ui.OpenScreen(surface, "asset://ui/loose.sui").IsValid());
}

TEST(UiRuntimeStage2, APackageNamingAnUnresolvableResourceDoesNotOpenHalfDressed)
{
    TempAssetRoot root;
    UiPackage package = MakeLayoutPackage();
    package.Resources.push_back(AssetRef{ AssetType::Font, "asset://ui/absent.ttf" });
    WritePackage(root, "ui/layout.sui", package);

    UiTestHost host(root);
    UiService& ui = host.Service();
    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });

    EXPECT_FALSE(ui.OpenScreen(surface, "asset://ui/layout.sui").IsValid())
        << "a document laid out against fonts it does not have measures wrong, "
           "which is harder to diagnose than a refusal";
}

TEST(UiRuntimeStage2, SurfacesResizeAndScreensRelayOut)
{
    TempAssetRoot root;
    UiPackage package = MakeLayoutPackage();
    // Width as a percentage, so the measured box tracks the surface.
    package.Blobs[1].Bytes = BytesOf(
        "body { display: block; width: 100%; height: 100%; }\n"
        "#panel { display: block; width: 50%; height: 120px; }\n"
        "#inner { display: block; width: 10px; height: 10px; }\n");
    WritePackage(root, "ui/layout.sui", package);

    UiTestHost host(root);
    UiService& ui = host.Service();
    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, "asset://ui/layout.sui");
    ASSERT_TRUE(screen.IsValid());

    ui.Update();
    ASSERT_TRUE(ui.MeasureElement(screen, "panel").has_value());
    EXPECT_FLOAT_EQ(ui.MeasureElement(screen, "panel")->Width, 400.0f);

    ui.SetSurfaceSize(surface, RenderExtent{ 1200, 600 });
    EXPECT_EQ(ui.GetSurfaceSize(surface).Width, 1200u);
    ui.Update();

    EXPECT_FLOAT_EQ(ui.MeasureElement(screen, "panel")->Width, 600.0f)
        << "a retained document re-flows on resize; that is why UI scale is live";
}

TEST(UiRuntimeStage2, DestroyingASurfaceClosesTheScreensOnIt)
{
    TempAssetRoot root;
    WritePackage(root, "ui/layout.sui", MakeLayoutPackage());

    UiTestHost host(root);
    UiService& ui = host.Service();
    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, "asset://ui/layout.sui");
    ASSERT_TRUE(screen.IsValid());

    ui.DestroySurface(surface);

    EXPECT_FALSE(ui.IsScreenOpen(screen))
        << "a document outliving its context is a dangling pointer, not a leak";
    EXPECT_FALSE(ui.MeasureElement(screen, "panel").has_value());
}

TEST(UiRuntimeStage2, HandlesToAClosedScreenAndDestroyedSurfaceStayStale)
{
    TempAssetRoot root;
    WritePackage(root, "ui/layout.sui", MakeLayoutPackage());

    UiTestHost host(root);
    UiService& ui = host.Service();

    const UiSurfaceId first = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle firstScreen = ui.OpenScreen(first, "asset://ui/layout.sui");
    ASSERT_TRUE(firstScreen.IsValid());
    ui.DestroySurface(first);

    // The slots are reused; the generations must not be.
    const UiSurfaceId second = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle secondScreen = ui.OpenScreen(second, "asset://ui/layout.sui");
    ASSERT_TRUE(secondScreen.IsValid());

    EXPECT_NE(first, second);
    EXPECT_NE(firstScreen, secondScreen);
    EXPECT_FALSE(ui.IsScreenOpen(firstScreen));
    EXPECT_TRUE(ui.IsScreenOpen(secondScreen));
    EXPECT_EQ(ui.GetSurfaceSize(first).Width, 0u) << "a destroyed surface answers nothing";
}

TEST(UiRuntimeStage2, DisplayScaleIsLiveAndRelaysOutTheDocument)
{
    // The property that makes a retained document worth the machinery: a scale
    // change re-flows it. The ImGui shell latches UI scale at startup because a
    // baked font atlas cannot follow one.
    TempAssetRoot root;
    UiPackage package = MakeLayoutPackage();
    // dp units resolve against the ratio; px would not, which is the point.
    package.Blobs[1].Bytes = BytesOf(
        "body { display: block; width: 100%; height: 100%; }\n"
        "#panel { display: block; width: 100dp; height: 50dp; }\n"
        "#inner { display: block; width: 10px; height: 10px; }\n");
    WritePackage(root, "ui/layout.sui", package);

    UiTestHost host(root);
    UiService& ui = host.Service();
    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, "asset://ui/layout.sui");
    ASSERT_TRUE(screen.IsValid());

    EXPECT_FLOAT_EQ(ui.GetSurfaceScale(surface), 1.0f);
    ui.Update();
    ASSERT_TRUE(ui.MeasureElement(screen, "panel").has_value());
    EXPECT_FLOAT_EQ(ui.MeasureElement(screen, "panel")->Width, 100.0f);

    ui.SetSurfaceScale(surface, 2.0f);
    EXPECT_FLOAT_EQ(ui.GetSurfaceScale(surface), 2.0f);
    ui.Update();
    EXPECT_FLOAT_EQ(ui.MeasureElement(screen, "panel")->Width, 200.0f);
}

TEST(UiRuntimeStage2, AnImplausibleDisplayScaleIsClampedNotObeyed)
{
    // A zero or negative ratio collapses every authored length to nothing, with
    // no obvious cause to whoever has to debug the blank screen.
    TempAssetRoot root;
    WritePackage(root, "ui/layout.sui", MakeLayoutPackage());

    UiTestHost host(root);
    UiService& ui = host.Service();
    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });

    ui.SetSurfaceScale(surface, 0.0f);
    EXPECT_GT(ui.GetSurfaceScale(surface), 0.0f);

    ui.SetSurfaceScale(surface, -3.0f);
    EXPECT_GT(ui.GetSurfaceScale(surface), 0.0f);

    ui.SetSurfaceScale(surface, 1000.0f);
    EXPECT_LE(ui.GetSurfaceScale(surface), 8.0f);
}
