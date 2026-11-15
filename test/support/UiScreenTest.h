#pragma once

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
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

class UiScreenPackageRoot
{
public:
    UiScreenPackageRoot()
    {
        Root = std::filesystem::temp_directory_path()
               / ("sencha_ui_screen_test_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(Root / "ui");
    }
    ~UiScreenPackageRoot()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
    UiScreenPackageRoot(const UiScreenPackageRoot&) = delete;
    UiScreenPackageRoot& operator=(const UiScreenPackageRoot&) = delete;

    [[nodiscard]] bool WritePackage(std::string_view markup, std::string_view style) const
    {
        UiPackage package;
        package.RootDocumentName = "screen.rml";
        package.Blobs.push_back({ .VirtualName = "screen.rml", .SourcePath = "ui/screen.rml",
                                  .Kind = UiBlobKind::Document, .Bytes = BytesOf(markup) });
        package.Blobs.push_back({ .VirtualName = "screen.rcss", .SourcePath = "ui/screen.rcss",
                                  .Kind = UiBlobKind::StyleSheet, .Bytes = BytesOf(style) });
        std::vector<std::byte> bytes;
        if (!WriteSuiToBytes(package, bytes))
            return false;
        std::ofstream(Root / "ui" / "screen.sui", std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return true;
    }
    [[nodiscard]] std::string PathString() const { return Root.generic_string(); }

private:
    static std::vector<std::byte> BytesOf(std::string_view text)
    {
        std::vector<std::byte> out(text.size());
        std::memcpy(out.data(), text.data(), text.size());
        return out;
    }

    std::filesystem::path Root;
};

class UiScreenTest : public testing::Test
{
protected:
    [[nodiscard]] bool OpenScreen(std::string_view body, std::string_view style, RenderExtent surface)
    {
        const bool written = Root.WritePackage(
            std::string(R"(<rml><head><link type="text/rcss" href="screen.rcss"/></head><body>)")
                + std::string(body) + "</body></rml>",
            "body { display: block; width: 100%; height: 100%; } " + std::string(style));
        EXPECT_TRUE(written);
        if (!written)
            return false;
        ScanAssetsDirectory(Root.PathString(), Assets.Registry, Assets.Assets.Kinds());
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages, Assets.Fonts, nullptr);
        EXPECT_TRUE(Ui->IsReady());
        if (!Ui->IsReady())
            return false;
        Surface = Ui->CreateSurface("test", surface);
        Screen = Ui->OpenScreen(Surface, "asset://ui/screen.sui");
        EXPECT_TRUE(Screen.IsValid());
        return Screen.IsValid();
    }

    void TearDown() override
    {
        if (Ui)
            Ui->Shutdown();
    }

    UiScreenPackageRoot Root;
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets{ Logging, Serializers, RuntimeAssets::ReferenceOnly{} };
    std::unique_ptr<UiService> Ui;
    UiSurfaceId Surface;
    UiScreenHandle Screen;
};
