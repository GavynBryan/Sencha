#include <gtest/gtest.h>

#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageSerializer.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <vector>

// What extraction has to guarantee before a renderer can be built on it:
// a submitted frame is self-contained, and stays valid no matter what the
// document does afterwards. Everything here is device-free -- recording is
// CPU work, which is why the renderer can be written against it rather than
// entangled with it.

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
             / ("sencha_ui_extract_test_" + std::to_string(rd()));
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

// Two solid blocks, so extraction has real geometry to record without needing a
// font face.
constexpr std::string_view kMarkup = R"(<rml>
<head><link type="text/rcss" href="theme.rcss"/></head>
<body>
    <div id="a"/>
    <div id="b"/>
</body>
</rml>)";

constexpr std::string_view kStyle = R"(
body { display: block; width: 100%; height: 100%; }
#a { display: block; width: 120px; height: 40px; background-color: #ff0000; }
#b { display: block; width: 60px; height: 20px; background-color: #00ff0080; }
)";

UiPackage MakePackage()
{
    UiPackage package;
    package.RootDocumentName = "screen.rml";

    UiPackageBlob root;
    root.VirtualName = "screen.rml";
    root.SourcePath = "ui/screen.rml";
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(kMarkup);
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = "theme.rcss";
    sheet.SourcePath = "ui/theme.rcss";
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(kStyle);
    package.Blobs.push_back(std::move(sheet));

    return package;
}

class UiTestHost
{
public:
    explicit UiTestHost(const TempAssetRoot& root)
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        ScanAssetsDirectory(root.PathString(), Assets.Registry, Assets.Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages,
                                         Assets.Fonts, nullptr);
    }
    UiService& Service() { return *Ui; }

private:
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
};

std::size_t TotalIndices(const UiDrawFrame& frame)
{
    std::size_t total = 0;
    for (const UiDrawCommand& command : frame.Commands)
    {
        if (command.Geometry)
            total += command.Geometry->Indices.size();
    }
    return total;
}
} // namespace

TEST(UiExtraction, ALaidOutDocumentRecordsGeometry)
{
    TempAssetRoot root;
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSuiToBytes(MakePackage(), bytes));
    root.WriteBytes("ui/screen.sui", bytes);

    UiTestHost host(root);
    UiService& ui = host.Service();
    ASSERT_TRUE(ui.IsReady());

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    ASSERT_TRUE(ui.OpenScreen(surface, "asset://ui/screen.sui").IsValid());

    ui.Update();
    ui.ExtractRender();

    ASSERT_EQ(ui.Frames().size(), 1u);
    const UiDrawFrame& frame = ui.Frames().front();

    EXPECT_EQ(frame.Surface.Width, 800u);
    EXPECT_EQ(frame.Surface.Height, 600u);
    ASSERT_FALSE(frame.Commands.empty()) << "two coloured blocks recorded nothing";

    // Every command must carry geometry that can actually be drawn.
    for (const UiDrawCommand& command : frame.Commands)
    {
        ASSERT_TRUE(command.Geometry) << "a command with no geometry reached the frame";
        EXPECT_FALSE(command.Geometry->Vertices.empty());
        EXPECT_EQ(command.Geometry->Indices.size() % 3, 0u) << "indices are triangle lists";
        for (const std::uint32_t index : command.Geometry->Indices)
            EXPECT_LT(index, command.Geometry->Vertices.size()) << "index out of range";
    }
    EXPECT_GT(TotalIndices(frame), 0u);
}

TEST(UiExtraction, ExtractingTwiceReplacesRatherThanAccumulates)
{
    TempAssetRoot root;
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSuiToBytes(MakePackage(), bytes));
    root.WriteBytes("ui/screen.sui", bytes);

    UiTestHost host(root);
    UiService& ui = host.Service();
    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    ASSERT_TRUE(ui.OpenScreen(surface, "asset://ui/screen.sui").IsValid());

    ui.Update();
    ui.ExtractRender();
    const std::size_t first = TotalIndices(ui.Frames().front());

    ui.Update();
    ui.ExtractRender();
    ASSERT_EQ(ui.Frames().size(), 1u);
    EXPECT_EQ(TotalIndices(ui.Frames().front()), first)
        << "a frame that grows every extract is one that never got reset";
}

TEST(UiExtraction, ASubmittedFrameOutlivesTheScreenItCameFrom)
{
    // The self-containment contract. A frame in flight must stay readable after
    // the document releases its geometry, or the renderer would be reading
    // freed memory one frame behind.
    TempAssetRoot root;
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSuiToBytes(MakePackage(), bytes));
    root.WriteBytes("ui/screen.sui", bytes);

    UiTestHost host(root);
    UiService& ui = host.Service();
    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, "asset://ui/screen.sui");
    ASSERT_TRUE(screen.IsValid());

    ui.Update();
    ui.ExtractRender();
    ASSERT_FALSE(ui.Frames().empty());

    // Take our own copy, as a render feature holding the frame across a frame
    // boundary would.
    const UiDrawFrame held = ui.Frames().front();
    ASSERT_FALSE(held.Commands.empty());
    const std::size_t heldIndices = TotalIndices(held);
    ASSERT_GT(heldIndices, 0u);

    ui.CloseScreen(screen);
    ui.Update();
    ui.ExtractRender();

    // The document and all its geometry are gone; the copy is not.
    EXPECT_TRUE(ui.Frames().empty());
    EXPECT_EQ(TotalIndices(held), heldIndices)
        << "geometry a submitted frame references was freed under it";
    for (const UiDrawCommand& command : held.Commands)
    {
        ASSERT_TRUE(command.Geometry);
        for (const std::uint32_t index : command.Geometry->Indices)
            EXPECT_LT(index, command.Geometry->Vertices.size());
    }
}

TEST(UiExtraction, AClosedScreenLeavesNoGeometryBehind)
{
    TempAssetRoot root;
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSuiToBytes(MakePackage(), bytes));
    root.WriteBytes("ui/screen.sui", bytes);

    UiTestHost host(root);
    UiService& ui = host.Service();
    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, "asset://ui/screen.sui");
    ASSERT_TRUE(screen.IsValid());

    ui.Update();
    ui.ExtractRender();

    ui.CloseScreen(screen);
    ui.Update();
    ui.ExtractRender();
    EXPECT_TRUE(ui.Frames().empty());
}

TEST(UiExtraction, ASurfaceThatDrewNothingProducesNoFrame)
{
    TempAssetRoot root;
    UiTestHost host(root);
    UiService& ui = host.Service();

    (void)ui.CreateSurface("empty", RenderExtent{ 800, 600 });
    ui.Update();
    ui.ExtractRender();

    EXPECT_TRUE(ui.Frames().empty())
        << "an empty surface must cost the renderer nothing, not an empty frame";
}

TEST(UiExtraction, AnUndeclaredImageIsRefusedRatherThanFetched)
{
    // A document naming an image its package never declared. The resolver must
    // not go looking: at render time there is no authoring context left to
    // report against, and the reference would work only on machines where that
    // file happens to exist.
    TempAssetRoot root;
    UiPackage package = MakePackage();
    package.Blobs[0].Bytes = BytesOf(R"(<rml>
<head><link type="text/rcss" href="theme.rcss"/></head>
<body><img id="pic" src="undeclared.png"/></body>
</rml>)");
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSuiToBytes(package, bytes));
    root.WriteBytes("ui/screen.sui", bytes);
    // Present on disk, and still must not resolve.
    root.WriteBytes("ui/undeclared.png", BytesOf("not really a png"));

    UiTestHost host(root);
    UiService& ui = host.Service();
    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    const UiScreenHandle screen = ui.OpenScreen(surface, "asset://ui/screen.sui");
    ASSERT_TRUE(screen.IsValid()) << "an unresolved image must not stop the document opening";

    ui.Update();
    ui.ExtractRender();

    for (const UiDrawFrame& frame : ui.Frames())
    {
        for (const UiDrawCommand& command : frame.Commands)
        {
            EXPECT_NE(command.Texture.Kind, UiTextureKind::Content)
                << "an undeclared image resolved to a content texture anyway";
        }
    }
}

namespace
{
// A package whose text needs a face the package itself declares, so opening it
// is the only reason that face resolves.
UiPackage MakeFontPackage(std::string_view rootName)
{
    constexpr std::string_view markup = R"(<rml>
<head><link type="text/rcss" href="typed.rcss"/></head>
<body><div id="line">Measured</div></body>
</rml>)";
    constexpr std::string_view style = R"(
@font-face { src: "fonts/Inter.sfont"; }
body { display: block; width: 100%; height: 100%; }
#line { display: inline-block; font-family: Inter; font-size: 32px; color: #ffffff; }
)";

    UiPackage package;
    package.RootDocumentName = std::string(rootName);

    UiPackageBlob root;
    root.VirtualName = std::string(rootName);
    root.SourcePath = "ui/" + std::string(rootName);
    root.Kind = UiBlobKind::Document;
    root.Bytes = BytesOf(markup);
    package.Blobs.push_back(std::move(root));

    UiPackageBlob sheet;
    sheet.VirtualName = "typed.rcss";
    sheet.SourcePath = "ui/typed.rcss";
    sheet.Kind = UiBlobKind::StyleSheet;
    sheet.Bytes = BytesOf(style);
    package.Blobs.push_back(std::move(sheet));

    package.Resources.push_back(AssetRef{ AssetType::Font,
                                          "asset://ui/fonts/Inter.sfont" });
    return package;
}

// The cooked face the repository already ships for UI tests, written where the
// directory scan will find it. Cooked artifacts normally reach the registry
// through .cooked/index.json under their authored path; these tests take the
// simpler route the .sui packages here already take and put the runtime file at
// the path the document names.
void InstallFontFixture(const TempAssetRoot& root)
{
    const std::filesystem::path cookedFace =
        std::filesystem::path(SENCHA_REPO_ROOT)
        / "test/fixtures/content/assets/.cooked/ui/fonts/Inter-Regular.ttf.sfont";
    std::ifstream cooked(cookedFace, std::ios::binary);
    ASSERT_TRUE(cooked.is_open()) << "cooked font fixture missing: " << cookedFace;
    const std::string bytes((std::istreambuf_iterator<char>(cooked)), {});
    ASSERT_FALSE(bytes.empty());

    root.WriteBytes("ui/fonts/Inter.sfont",
                    std::span(reinterpret_cast<const std::byte*>(bytes.data()),
                              bytes.size()));
}
} // namespace

TEST(UiExtraction, TwoScreensShareASurfaceAndEachKeepsItsOwnResources)
{
    // A surface carrying a stack -- a HUD with a menu over it -- is two
    // documents in one context. Nothing had ever opened two, so this is the
    // shape the pause shell introduces: the upper screen declares a face the
    // lower one does not, and has to lay out with it.
    TempAssetRoot root;
    InstallFontFixture(root);

    std::vector<std::byte> plain;
    ASSERT_TRUE(WriteSuiToBytes(MakePackage(), plain));
    root.WriteBytes("ui/plain.sui", plain);

    std::vector<std::byte> typed;
    ASSERT_TRUE(WriteSuiToBytes(MakeFontPackage("typed.rml"), typed));
    root.WriteBytes("ui/typed.sui", typed);

    UiTestHost host(root);
    UiService& ui = host.Service();
    ASSERT_TRUE(ui.IsReady());

    const UiSurfaceId surface = ui.CreateSurface("test", RenderExtent{ 800, 600 });
    // The plain one first, so it is the screen the old scope would have picked.
    ASSERT_TRUE(ui.OpenScreen(surface, "asset://ui/plain.sui").IsValid());
    const UiScreenHandle over = ui.OpenScreen(surface, "asset://ui/typed.sui");
    ASSERT_TRUE(over.IsValid());

    ui.Update();
    ui.ExtractRender();

    const std::optional<UiElementBox> line = ui.MeasureElement(over, "line");
    ASSERT_TRUE(line.has_value());
    // Shrink-to-fit, so the width is the measured advance of the glyphs. With no
    // face resolved there are no advances and the box collapses -- which is what
    // scoping resolution to the first screen on the surface produced.
    EXPECT_GT(line->Width, 0.0f)
        << "the upper screen's text measured nothing: its own face did not resolve";

    // And the lower one is still there and still drawing, rather than having
    // been displaced by the screen opened over it.
    ui.ExtractRender();
    ASSERT_EQ(ui.Frames().size(), 1u) << "one surface still records one frame";
    EXPECT_GT(TotalIndices(ui.Frames().front()), 0u);
}
