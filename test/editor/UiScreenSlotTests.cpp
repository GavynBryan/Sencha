#include "ui/UiScreenSlot.h"

#include <assets/runtime/RuntimeAssets.h>
#include <assets/ui/UiPackage.h>
#include <assets/ui/UiPackageSerializer.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

// A presenter's one screen: it opens only once it has a surface, follows the
// presenter to another surface while open, and is closed when it goes.
namespace
{
constexpr std::string_view kMarkup = "<rml><body/></rml>";

class SlotHost
{
public:
    SlotHost()
        : Root(std::filesystem::temp_directory_path() / ("sencha_screen_slot_" + std::to_string(std::random_device{}())))
        , Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        UiPackage package;
        package.RootDocumentName = "slot.rml";
        UiPackageBlob root;
        root.VirtualName = "slot.rml";
        root.SourcePath = "ui/slot.rml";
        root.Kind = UiBlobKind::Document;
        root.Bytes.resize(kMarkup.size());
        std::memcpy(root.Bytes.data(), kMarkup.data(), kMarkup.size());
        package.Blobs.push_back(std::move(root));
        std::vector<std::byte> bytes;
        EXPECT_TRUE(WriteSuiToBytes(package, bytes));
        std::filesystem::create_directories(Root / "ui");
        std::ofstream(Root / "ui" / "slot.sui", std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages, Assets.Fonts);
    }

    ~SlotHost()
    {
        Ui.reset();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    std::filesystem::path Root;
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
};

UiScreenDesc SlotScreen()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://ui/slot.sui";
    return desc;
}
} // namespace

TEST(UiScreenSlot, OpensOnItsSurfaceFollowsAMoveAndClosesWhenItGoes)
{
    SlotHost host;
    UiService& ui = *host.Ui;
    ASSERT_TRUE(ui.IsReady());
    const UiSurfaceId first = ui.CreateSurface("first", RenderExtent{ 320, 200 });
    const UiSurfaceId second = ui.CreateSurface("second", RenderExtent{ 320, 200 });

    UiScreenHandle last;
    {
        UiScreenSlot slot(ui, SlotScreen);
        EXPECT_FALSE(slot.Open()) << "nowhere to open yet";
        slot.MoveTo(first);
        ASSERT_TRUE(slot.Open());
        const UiScreenHandle onFirst = slot.Handle();

        slot.MoveTo(second);
        EXPECT_TRUE(slot.IsOpen()) << "moved while open, it opens where it went";
        EXPECT_FALSE(ui.IsScreenOpen(onFirst));
        EXPECT_EQ(slot.Surface(), second);

        slot.Close();
        slot.MoveTo(first);
        EXPECT_FALSE(slot.IsOpen()) << "moved while closed, it stays closed";
        ASSERT_TRUE(slot.Open());
        last = slot.Handle();
    }
    EXPECT_FALSE(ui.IsScreenOpen(last)) << "it closes when it goes";
}
