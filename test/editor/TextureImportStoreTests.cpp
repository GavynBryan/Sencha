#include "project/TextureImportStore.h"

#include <assets/cook/ImportOnDemand.h>
#include <assets/cook/TextureCook.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    // A real 4x4 RGBA PNG (red/blue checker), so the end-to-end test runs the
    // actual decoder and cook rather than a stand-in importer.
    constexpr uint8_t kTinyPng[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x08, 0x06, 0x00, 0x00, 0x00, 0xa9, 0xf1, 0x9e,
        0x7e, 0x00, 0x00, 0x00, 0x1a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x84, 0xa1, 0xd4, 0x7f, 0x06, 0x0c, 0x01, 0x18, 0x03, 0x21, 0x81, 0x26, 0x00, 0x00, 0x2c,
        0x50, 0x1f, 0xe1, 0x24, 0xe3, 0x2b, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
        0x42, 0x60, 0x82,
    };

    class TextureImportStoreTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Root = std::filesystem::temp_directory_path() / "sencha_texture_import_store_tests";
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root / "textures");
            std::ofstream png(Root / "textures/pixel.png", std::ios::binary);
            png.write(reinterpret_cast<const char*>(kTinyPng), sizeof(kTinyPng));
        }

        void TearDown() override
        {
            std::filesystem::remove_all(Root);
        }

        std::filesystem::path Root;
        std::vector<std::string> Roots() const { return { Root.string() }; }
    };
}

TEST_F(TextureImportStoreTest, ResolvesSourceFromVirtualPathNotRegistry)
{
    const auto source = ResolveTextureSource(Roots(), "asset://textures/pixel.png");
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ(source->RelPath, "textures/pixel.png");
    EXPECT_TRUE(source->MetaFile().ends_with("textures/pixel.png.meta"));
    EXPECT_TRUE(source->CookedFile().ends_with(".cooked/textures/pixel.png.stex"));

    EXPECT_FALSE(ResolveTextureSource(Roots(), "asset://textures/missing.png").has_value());
    EXPECT_FALSE(ResolveTextureSource(Roots(), "textures/pixel.png").has_value());
}

TEST_F(TextureImportStoreTest, EndToEndNearestSettingsRecookTheArtifact)
{
    // The user flow, headless: mount (cook with defaults), verify linear BC7;
    // apply nearest/uncompressed/no-mips settings; recook; verify the artifact
    // header changed accordingly; reload settings the way the dialog seeds.
    PngTextureImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));
    LoggingProvider logging;

    {
        AssetRegistry registry(logging);
        ASSERT_TRUE(ImportAssetsOnDemand(Root.string(), importers, registry, logging));
    }

    const auto source = ResolveTextureSource(Roots(), "asset://textures/pixel.png");
    ASSERT_TRUE(source.has_value());

    CookedTextureState state = ReadCookedTextureState(*source);
    ASSERT_TRUE(state.Exists);
    EXPECT_EQ(state.Format, TexturePixelFormat::BC7_SRGB);
    EXPECT_EQ(state.Filter, TextureFilter::Linear);
    EXPECT_EQ(state.MipCount, 3u); // 4x4 full chain

    TextureImportSettings settings;
    settings.Filter = TextureFilter::Nearest;
    settings.Compress = false;
    settings.GenerateMips = false;
    std::string error;
    ASSERT_TRUE(SaveTextureImportSettingsFor(*source, settings, &error)) << error;

    std::vector<std::string> artifacts;
    ASSERT_TRUE(ReimportOneSource(Root.string(), source->RelPath, importers, logging, artifacts));

    state = ReadCookedTextureState(*source);
    ASSERT_TRUE(state.Exists);
    EXPECT_EQ(state.Format, TexturePixelFormat::RGBA8_SRGB);
    EXPECT_EQ(state.Filter, TextureFilter::Nearest);
    EXPECT_EQ(state.MipCount, 1u);
    EXPECT_EQ(state.Width, 4u);

    // The dialog's seed path reads back exactly what was applied.
    const TextureImportSettings loaded = LoadTextureImportSettingsFor(*source, &error);
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_EQ(loaded, settings);

    // The description line a UI shows carries the ground truth.
    EXPECT_EQ(DescribeCookedTextureState(state), "4x4, RGBA8 sRGB, 1 mip, nearest (base_color)");
}

TEST_F(TextureImportStoreTest, MalformedSidecarReportsAndYieldsDefaults)
{
    const auto source = ResolveTextureSource(Roots(), "asset://textures/pixel.png");
    ASSERT_TRUE(source.has_value());
    {
        std::ofstream meta(source->MetaFile());
        meta << "{ broken";
    }

    std::string error;
    const TextureImportSettings loaded = LoadTextureImportSettingsFor(*source, &error);
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(loaded, TextureImportSettings{});
}
