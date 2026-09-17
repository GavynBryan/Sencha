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

// Where a surface's recording goes. Exactly one place: the list the window
// feature draws, or the by-surface entry a host feature drawing into its own
// target asks for. Never both, never neither.

namespace
{
std::vector<std::byte> BytesOf(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

struct Fixture
{
    std::filesystem::path Root;
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    std::unique_ptr<RuntimeAssets> Assets;
    std::unique_ptr<UiService> Ui;

    Fixture()
    {
        std::random_device rd;
        Root = std::filesystem::temp_directory_path() / ("sencha_ui_dest_" + std::to_string(rd()));
        std::filesystem::create_directories(Root / "ui");

        UiPackage package;
        package.RootDocumentName = "doc.rml";
        UiPackageBlob root;
        root.VirtualName = "doc.rml";
        root.SourcePath = "ui/doc.rml";
        root.Kind = UiBlobKind::Document;
        root.Bytes = BytesOf(R"(<rml><head><link type="text/rcss" href="doc.rcss"/></head>
<body><div id="box"/></body></rml>)");
        package.Blobs.push_back(std::move(root));
        UiPackageBlob sheet;
        sheet.VirtualName = "doc.rcss";
        sheet.SourcePath = "ui/doc.rcss";
        sheet.Kind = UiBlobKind::StyleSheet;
        sheet.Bytes = BytesOf("body { display: block; width: 100%; height: 100%; }"
                              "#box { display: block; width: 100px; height: 100px; background-color: #ff0000; }");
        package.Blobs.push_back(std::move(sheet));
        std::vector<std::byte> bytes;
        EXPECT_TRUE(WriteSuiToBytes(package, bytes));
        std::ofstream(Root / "ui" / "doc.sui", std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

        Assets = std::make_unique<RuntimeAssets>(Logging, Serializers, RuntimeAssets::ReferenceOnly{});
        ScanAssetsDirectory(Root.generic_string(), Assets->Registry, Assets->Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets->Assets, Assets->UiPackages,
                                         Assets->Fonts, nullptr, nullptr);
    }
    ~Fixture()
    {
        Ui->Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    UiSurfaceId OpenOn(const char* name)
    {
        const UiSurfaceId surface = Ui->CreateSurface(name, RenderExtent{ 640, 480 });
        EXPECT_TRUE(Ui->OpenScreen(surface, "asset://ui/doc.sui").IsValid());
        return surface;
    }
    void Frame()
    {
        Ui->Update();
        Ui->ExtractRender();
    }
};
} // namespace

TEST(UiDestination, TheDefaultIsTheWindowAndTheWindowListCarriesIt)
{
    Fixture f;
    const UiSurfaceId a = f.OpenOn("a");
    f.Frame();
    EXPECT_EQ(f.Ui->GetSurfaceDestination(a), UiSurfaceDestination::Window);
    EXPECT_EQ(f.Ui->Frames().size(), 1u);
    EXPECT_EQ(f.Ui->OffscreenFrame(a), nullptr);
}

TEST(UiDestination, AnOffscreenSurfaceLeavesTheWindowListAndIsFoundByName)
{
    Fixture f;
    const UiSurfaceId window = f.OpenOn("window");
    const UiSurfaceId preview = f.OpenOn("preview");
    f.Ui->SetSurfaceDestination(preview, UiSurfaceDestination::Offscreen);
    f.Frame();

    // The window feature draws one; the host feature asks for the other.
    EXPECT_EQ(f.Ui->Frames().size(), 1u) << "the offscreen surface was also handed to the window";
    const UiDrawFrame* offscreen = f.Ui->OffscreenFrame(preview);
    ASSERT_NE(offscreen, nullptr);
    EXPECT_FALSE(offscreen->IsEmpty());
    EXPECT_EQ(offscreen->Surface.Width, 640u);
    EXPECT_EQ(f.Ui->OffscreenFrame(window), nullptr) << "a window surface has no offscreen recording";
}

TEST(UiDestination, SwitchingMovesTheRecordingAtTheNextExtract)
{
    Fixture f;
    const UiSurfaceId a = f.OpenOn("a");
    f.Frame();
    ASSERT_EQ(f.Ui->Frames().size(), 1u);

    f.Ui->SetSurfaceDestination(a, UiSurfaceDestination::Offscreen);
    // Not yet: the recording published this frame is the one already made.
    EXPECT_EQ(f.Ui->Frames().size(), 1u);
    f.Frame();
    EXPECT_TRUE(f.Ui->Frames().empty());
    EXPECT_NE(f.Ui->OffscreenFrame(a), nullptr);

    f.Ui->SetSurfaceDestination(a, UiSurfaceDestination::Window);
    f.Frame();
    EXPECT_EQ(f.Ui->Frames().size(), 1u);
    EXPECT_EQ(f.Ui->OffscreenFrame(a), nullptr);
}

TEST(UiDestination, AnUnknownSurfaceIsWindowDestinedAndHasNoRecording)
{
    Fixture f;
    EXPECT_EQ(f.Ui->GetSurfaceDestination(UiSurfaceId{}), UiSurfaceDestination::Window);
    EXPECT_EQ(f.Ui->OffscreenFrame(UiSurfaceId{}), nullptr);
}
