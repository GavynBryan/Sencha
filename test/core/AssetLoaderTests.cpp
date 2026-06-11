#include <assets/material/MaterialAssetLoader.h>
#include <assets/static_mesh/StaticMeshAssetLoader.h>
#include <assets/static_mesh/StaticMeshSerializer.h>
#include <assets/texture/TextureAssetLoader.h>
#include <core/assets/AssetInFlightTable.h>
#include <core/assets/AssetLoader.h>
#include <core/assets/AssetSource.h>
#include <core/assets/AssetSystem.h>
#include <core/logging/LoggingProvider.h>
#include <render/Image.h>
#include <render/MaterialCache.h>
#include <render/static_mesh/StaticMeshPrimitives.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace
{
    // The zero-thread determinism harness for loader stage halves: bytes come
    // from memory, no filesystem, no threads.
    class MemoryAssetSource final : public IAssetSource
    {
    public:
        bool ReadBytes(std::string_view filePath, std::vector<std::byte>& out) override
        {
            auto it = Files.find(std::string(filePath));
            if (it == Files.end())
                return false;
            out = it->second;
            return true;
        }

        void Add(std::string_view path, std::vector<std::byte> bytes)
        {
            Files[std::string(path)] = std::move(bytes);
        }

        void Add(std::string_view path, std::string_view text)
        {
            const auto* data = reinterpret_cast<const std::byte*>(text.data());
            Files[std::string(path)] = std::vector<std::byte>(data, data + text.size());
        }

    private:
        std::map<std::string, std::vector<std::byte>> Files;
    };

    AssetRecord MakeFileRecord(AssetType type, std::string_view path)
    {
        return AssetRecord{
            .Type = type,
            .SourceKind = AssetSourceKind::File,
            .Path = std::string(path),
            .FilePath = std::string(path),
        };
    }

    // 16x16 grayscale checker, 8-bit RGB PNG (the CubeDemo fixture, embedded).
    constexpr uint8_t kCheckerPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x91, 0x68, 0x36, 0x00, 0x00, 0x00,
        0x20, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0xF8, 0x8F, 0x04, 0x16,
        0x20, 0x01, 0x5C, 0xE2, 0x0C, 0x83, 0x50, 0x03, 0x31, 0x8A, 0x90, 0xC5,
        0x07, 0xA3, 0x86, 0xD1, 0x78, 0x18, 0x14, 0x1A, 0x00, 0x6E, 0xE7, 0x6E,
        0x9F, 0x05, 0xEC, 0xA4, 0x18, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
        0x44, 0xAE, 0x42, 0x60, 0x82,
    };

    std::vector<std::byte> CheckerPngBytes()
    {
        const auto* data = reinterpret_cast<const std::byte*>(kCheckerPng);
        return { data, data + sizeof(kCheckerPng) };
    }
} // namespace

// -- FileAssetSource ------------------------------------------------------------

TEST(AssetSource, FileSourceReadsBytes)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "sencha_asset_source_test.bin";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "sencha";
    }

    FileAssetSource source;
    std::vector<std::byte> bytes;
    ASSERT_TRUE(source.ReadBytes(path.generic_string(), bytes));
    EXPECT_EQ(bytes.size(), 6u);
    EXPECT_EQ(static_cast<char>(bytes[0]), 's');

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(AssetSource, FileSourceFailsOnMissingFile)
{
    FileAssetSource source;
    std::vector<std::byte> bytes;
    EXPECT_FALSE(source.ReadBytes("does/not/exist.bin", bytes));
}

TEST(AssetSource, ReadAssetBytesFallsBackToVirtualPath)
{
    MemoryAssetSource source;
    source.Add("asset://textures/dev/only_virtual.png", "x");

    AssetRecord record = MakeFileRecord(AssetType::Texture, "asset://textures/dev/only_virtual.png");
    record.FilePath.clear();

    std::vector<std::byte> bytes;
    EXPECT_TRUE(ReadAssetBytes(source, record, bytes));
    EXPECT_EQ(bytes.size(), 1u);
}

// -- AssetInFlightTable ----------------------------------------------------------

TEST(AssetInFlightTable, FirstBeginStartsLaterBeginsJoin)
{
    AssetInFlightTable table;
    EXPECT_EQ(table.Begin("asset://a"), AssetInFlightTable::BeginResult::Started);
    EXPECT_EQ(table.Begin("asset://a"), AssetInFlightTable::BeginResult::Joined);
    EXPECT_EQ(table.Begin("asset://a"), AssetInFlightTable::BeginResult::Joined);
    EXPECT_TRUE(table.IsInFlight("asset://a"));
    EXPECT_EQ(table.Begin("asset://b"), AssetInFlightTable::BeginResult::Started);
    EXPECT_EQ(table.Size(), 2u);
}

TEST(AssetInFlightTable, FinishReturnsRequesterCountAndClears)
{
    AssetInFlightTable table;
    (void)table.Begin("asset://a");
    (void)table.Begin("asset://a");
    (void)table.Begin("asset://a");

    EXPECT_EQ(table.Finish("asset://a"), 3u);
    EXPECT_FALSE(table.IsInFlight("asset://a"));
    EXPECT_EQ(table.Finish("asset://a"), 0u);
    EXPECT_EQ(table.Finish("asset://never"), 0u);
}

// -- StaticMeshAssetLoader --------------------------------------------------------

TEST(StaticMeshAssetLoader, StagesSerializedMeshFromMemory)
{
    LoggingProvider logging;
    StaticMeshSerializer serializer(logging);
    std::vector<std::byte> bytes;
    ASSERT_TRUE(serializer.WriteToBytes(StaticMeshPrimitives::BuildCube(1.0f), bytes));

    MemoryAssetSource source;
    source.Add("asset://meshes/dev/cube.smesh", std::move(bytes));

    StaticMeshAssetLoader loader(logging, nullptr);
    AssetStaging staging = loader.LoadStaged(
        MakeFileRecord(AssetType::StaticMesh, "asset://meshes/dev/cube.smesh"), source);

    ASSERT_TRUE(staging.IsValid()) << staging.Error;
    const auto* data = std::any_cast<StaticMeshData>(&staging.Payload);
    ASSERT_NE(data, nullptr);
    EXPECT_FALSE(data->Vertices.empty());
    EXPECT_FALSE(data->Indices.empty());

    // No cache: commit fails cleanly rather than crashing.
    EXPECT_FALSE(loader.CommitTyped(std::move(staging)).IsValid());
}

TEST(StaticMeshAssetLoader, StagingFailsOnMissingAndMalformedBytes)
{
    LoggingProvider logging;
    StaticMeshAssetLoader loader(logging, nullptr);
    MemoryAssetSource source;
    source.Add("asset://meshes/dev/garbage.smesh", "this is not a mesh");

    AssetStaging missing = loader.LoadStaged(
        MakeFileRecord(AssetType::StaticMesh, "asset://meshes/dev/absent.smesh"), source);
    EXPECT_FALSE(missing.IsValid());
    EXPECT_FALSE(missing.Error.empty());

    AssetStaging malformed = loader.LoadStaged(
        MakeFileRecord(AssetType::StaticMesh, "asset://meshes/dev/garbage.smesh"), source);
    EXPECT_FALSE(malformed.IsValid());
    EXPECT_FALSE(malformed.Error.empty());
}

// -- TextureAssetLoader ------------------------------------------------------------

TEST(TextureAssetLoader, StagesPngFromMemoryWithUsageColorspace)
{
    LoggingProvider logging;
    TextureAssetLoader loader(logging, nullptr);
    MemoryAssetSource source;
    source.Add("asset://textures/dev/checker.png", CheckerPngBytes());

    const AssetRecord record = MakeFileRecord(AssetType::Texture, "asset://textures/dev/checker.png");

    AssetStaging srgb = loader.LoadStaged(record, source, /*srgb*/ true);
    ASSERT_TRUE(srgb.IsValid()) << srgb.Error;
    const auto* srgbImage = std::any_cast<Image>(&srgb.Payload);
    ASSERT_NE(srgbImage, nullptr);
    EXPECT_EQ(srgbImage->Width, 16u);
    EXPECT_EQ(srgbImage->Height, 16u);
    EXPECT_EQ(srgbImage->Format, PixelFormat::RGBA8_SRGB);

    AssetStaging linear = loader.LoadStaged(record, source, /*srgb*/ false);
    ASSERT_TRUE(linear.IsValid()) << linear.Error;
    EXPECT_EQ(std::any_cast<Image>(&linear.Payload)->Format, PixelFormat::RGBA8);

    // The generic (driver-facing) overload assumes sRGB until .stex usage tags.
    AssetStaging generic = static_cast<IAssetLoader&>(loader).LoadStaged(record, source);
    ASSERT_TRUE(generic.IsValid());
    EXPECT_EQ(std::any_cast<Image>(&generic.Payload)->Format, PixelFormat::RGBA8_SRGB);

    // No cache: commit fails cleanly.
    EXPECT_FALSE(loader.CommitTyped(std::move(srgb)).IsValid());
}

TEST(TextureAssetLoader, StagingFailsOnUndecodableBytes)
{
    LoggingProvider logging;
    TextureAssetLoader loader(logging, nullptr);
    MemoryAssetSource source;
    source.Add("asset://textures/dev/garbage.png", "not a png");

    AssetStaging staging = loader.LoadStaged(
        MakeFileRecord(AssetType::Texture, "asset://textures/dev/garbage.png"), source);
    EXPECT_FALSE(staging.IsValid());
    EXPECT_FALSE(staging.Error.empty());
}

// -- MaterialAssetLoader (full stage + commit, headless) ----------------------------

TEST(MaterialAssetLoader, StageAndCommitRoundTripHeadless)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);
    MaterialAssetLoader loader(logging, assets, &materials, nullptr);

    MemoryAssetSource source;
    source.Add("asset://materials/dev/test.smat", R"({
        "version": 1,
        "base_color_factor": [0.25, 0.5, 0.75, 1.0],
        "base_color_texture": "asset://textures/dev/checker.png",
        "metallic_factor": 0.6
    })");

    AssetStaging staging = loader.LoadStaged(
        MakeFileRecord(AssetType::Material, "asset://materials/dev/test.smat"), source);
    ASSERT_TRUE(staging.IsValid()) << staging.Error;

    MaterialHandle handle = loader.CommitTyped(std::move(staging));
    ASSERT_TRUE(handle.IsValid());

    const Material* material = materials.Get(handle);
    ASSERT_NE(material, nullptr);
    EXPECT_FLOAT_EQ(material->BaseColor.X, 0.25f);
    EXPECT_FLOAT_EQ(material->MetallicFactor, 0.6f);
    // No TextureCache: slot stays neutral.
    EXPECT_EQ(material->BaseColorTextureIndex, UINT32_MAX);
}

TEST(MaterialAssetLoader, StagingReportsParseErrors)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);
    MaterialAssetLoader loader(logging, assets, &materials, nullptr);

    MemoryAssetSource source;
    source.Add("asset://materials/dev/bad.smat", R"({"version": 1, "typo_key": 1})");

    AssetStaging staging = loader.LoadStaged(
        MakeFileRecord(AssetType::Material, "asset://materials/dev/bad.smat"), source);
    EXPECT_FALSE(staging.IsValid());
    EXPECT_NE(staging.Error.find("typo_key"), std::string::npos);
}

// -- Contract: commits reject foreign payloads ---------------------------------------

TEST(AssetLoaderContract, CommitRejectsMismatchedPayload)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);

    AssetStaging staging;
    staging.Record = MakeFileRecord(AssetType::Material, "asset://materials/dev/wrong.smat");
    staging.Payload = 42; // not a MaterialDescription

    MaterialAssetLoader matLoader(logging, assets, &materials, nullptr);
    EXPECT_FALSE(matLoader.CommitTyped(std::move(staging)).IsValid());

    AssetStaging meshStaging;
    meshStaging.Record = MakeFileRecord(AssetType::StaticMesh, "asset://meshes/dev/wrong.smesh");
    meshStaging.Payload = 42; // not a StaticMeshData

    StaticMeshAssetLoader meshLoader(logging, nullptr);
    EXPECT_FALSE(meshLoader.CommitTyped(std::move(meshStaging)).IsValid());
}
