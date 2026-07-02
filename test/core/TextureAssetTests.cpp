#include <assets/texture/TextureAssetLoader.h>
#include <assets/texture/TextureFormat.h>
#include <assets/texture/TextureLoader.h>
#include <assets/texture/TextureSerializer.h>
#include <core/assets/AssetSource.h>
#include <core/logging/LoggingProvider.h>
#include <render/Image.h>
#include <render/TextureData.h>

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/ImportOnDemand.h>
#include <assets/cook/TextureCook.h>
#include <assets/cook/TextureImportSettings.h>
#include <core/assets/AssetRegistry.h>
#include <bc7decomp.h>
#endif

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    Image MakeImage(uint32_t width, uint32_t height, PixelFormat format = PixelFormat::RGBA8_SRGB)
    {
        Image image;
        image.Width = width;
        image.Height = height;
        image.Format = format;
        image.Pixels.assign(uint64_t(width) * height * 4, 255);
        return image;
    }

    // Hand-built block-compressed TextureData: the BC fixture Decision E
    // requires from the first .stex commit. The block payload is patterned
    // bytes — containers and uploads care about sizes and offsets, not
    // whether the blocks decode to something pretty.
    TextureData MakeBlockCompressedFixture(TexturePixelFormat format, uint32_t size)
    {
        TextureData texture;
        texture.Format = format;
        texture.Usage = format == TexturePixelFormat::BC5 ? TextureUsage::Normal
                                                          : TextureUsage::BaseColor;
        texture.Width = size;
        texture.Height = size;

        uint64_t offset = 0;
        uint32_t w = size;
        uint32_t h = size;
        const uint32_t levels = FullMipChainLength(size, size);
        for (uint32_t i = 0; i < levels; ++i)
        {
            const uint64_t byteSize = TextureMipByteSize(format, w, h);
            texture.Mips.push_back(TextureMipLevel{ w, h, offset, byteSize });
            offset += byteSize;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }

        texture.Blob.resize(offset);
        for (uint64_t i = 0; i < offset; ++i)
            texture.Blob[i] = static_cast<uint8_t>(i * 31 + 7);
        return texture;
    }

#ifdef SENCHA_ENABLE_COOK
    Image MakeCheckerImage(uint32_t size)
    {
        Image image = MakeImage(size, size);
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                const uint8_t v = ((x + y) % 2 == 0) ? 255 : 0;
                uint8_t* p = image.Pixels.data() + (uint64_t(y) * size + x) * 4;
                p[0] = p[1] = p[2] = v;
                p[3] = 255;
            }
        return image;
    }
#endif
} // namespace

// -- TextureData structure -------------------------------------------------------

TEST(TextureData, MipMathHandlesBlockFormats)
{
    EXPECT_EQ(FullMipChainLength(16, 16), 5u);
    EXPECT_EQ(FullMipChainLength(8, 2), 4u);
    EXPECT_EQ(TextureMipByteSize(TexturePixelFormat::RGBA8, 16, 16), 16u * 16u * 4u);
    // Sub-block extents still occupy whole 4x4 blocks.
    EXPECT_EQ(TextureMipByteSize(TexturePixelFormat::BC7, 2, 2), 16u);
    EXPECT_EQ(TextureMipByteSize(TexturePixelFormat::BC7, 8, 8), 4u * 16u);
    EXPECT_EQ(TextureMipByteSize(TexturePixelFormat::BC4, 4, 4), 8u);
    EXPECT_EQ(TextureMipByteSize(TexturePixelFormat::BC5, 5, 5), 4u * 16u);
}

TEST(TextureData, ValidateCatchesStructuralLies)
{
    TextureData good = MakeBlockCompressedFixture(TexturePixelFormat::BC7_SRGB, 8);
    EXPECT_TRUE(ValidateTextureData(good));

    TextureData badOffset = good;
    badOffset.Mips[1].Offset += 1;
    EXPECT_FALSE(ValidateTextureData(badOffset));

    TextureData badSize = good;
    badSize.Mips[2].ByteSize -= 1;
    EXPECT_FALSE(ValidateTextureData(badSize));

    TextureData badBlob = good;
    badBlob.Blob.pop_back();
    EXPECT_FALSE(ValidateTextureData(badBlob));

    TextureData badExtent = good;
    badExtent.Mips[1].Width = 5;
    EXPECT_FALSE(ValidateTextureData(badExtent));
}

// -- .stex round trip --------------------------------------------------------------

namespace
{
    void ExpectTexturesEqual(const TextureData& a, const TextureData& b)
    {
        EXPECT_EQ(a.Format, b.Format);
        EXPECT_EQ(a.Usage, b.Usage);
        EXPECT_EQ(a.Width, b.Width);
        EXPECT_EQ(a.Height, b.Height);
        ASSERT_EQ(a.Mips.size(), b.Mips.size());
        for (std::size_t i = 0; i < a.Mips.size(); ++i)
        {
            EXPECT_EQ(a.Mips[i].Width, b.Mips[i].Width);
            EXPECT_EQ(a.Mips[i].Offset, b.Mips[i].Offset);
            EXPECT_EQ(a.Mips[i].ByteSize, b.Mips[i].ByteSize);
        }
        EXPECT_EQ(a.Blob, b.Blob);
    }
}

TEST(StexRoundTrip, BlockCompressedFixturesSurvive)
{
    for (const TexturePixelFormat format :
         { TexturePixelFormat::BC7_SRGB, TexturePixelFormat::BC5 })
    {
        const TextureData original = MakeBlockCompressedFixture(format, 8);
        ASSERT_TRUE(ValidateTextureData(original));

        std::vector<std::byte> bytes;
        ASSERT_TRUE(WriteStexToBytes(original, bytes));
        ASSERT_TRUE(LooksLikeStex(bytes.data(), bytes.size()));

        TextureData loaded;
        std::string error;
        ASSERT_TRUE(LoadStexFromBytes(bytes, loaded, &error)) << error;
        ExpectTexturesEqual(original, loaded);
    }
}

TEST(StexRoundTrip, RejectsInvalidDataAndCorruptContainers)
{
    TextureData invalid = MakeBlockCompressedFixture(TexturePixelFormat::BC7, 8);
    invalid.Blob.pop_back();
    std::vector<std::byte> bytes;
    EXPECT_FALSE(WriteStexToBytes(invalid, bytes));

    const TextureData good = MakeBlockCompressedFixture(TexturePixelFormat::BC7, 8);
    ASSERT_TRUE(WriteStexToBytes(good, bytes));

    TextureData out;
    std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + bytes.size() / 2);
    EXPECT_FALSE(LoadStexFromBytes(truncated, out));

    std::vector<std::byte> badMagic = bytes;
    badMagic[0] = std::byte{ 'X' };
    EXPECT_FALSE(LoadStexFromBytes(badMagic, out));
    EXPECT_FALSE(LooksLikeStex(badMagic.data(), badMagic.size()));

    std::vector<std::byte> badVersion = bytes;
    badVersion[4] = std::byte{ 0x7F };
    EXPECT_FALSE(LoadStexFromBytes(badVersion, out));
}

#ifdef SENCHA_ENABLE_COOK

// -- Texture cook -------------------------------------------------------------------

TEST(TextureCook, FullMipChainWithCorrectExtents)
{
    TextureData texture;
    ASSERT_TRUE(BuildTextureMipChainRgba8(MakeImage(16, 8), TextureUsage::BaseColor, texture));

    EXPECT_EQ(texture.Format, TexturePixelFormat::RGBA8_SRGB);
    EXPECT_EQ(texture.Usage, TextureUsage::BaseColor);
    ASSERT_EQ(texture.Mips.size(), 5u);
    EXPECT_EQ(texture.Mips[1].Width, 8u);
    EXPECT_EQ(texture.Mips[1].Height, 4u);
    EXPECT_EQ(texture.Mips[4].Width, 1u);
    EXPECT_EQ(texture.Mips[4].Height, 1u);
    EXPECT_TRUE(ValidateTextureData(texture));
}

TEST(TextureCook, SrgbDownsampleIsColorspaceCorrect)
{
    // A 2x2 black/white checker averaged in linear space is 0.5, which
    // re-encodes to ~188 in sRGB. A naive byte average would give ~128 —
    // the classic too-dark-mips bug this filter exists to prevent.
    const Image checker = MakeCheckerImage(2);

    TextureData srgbCooked;
    ASSERT_TRUE(BuildTextureMipChainRgba8(checker, TextureUsage::BaseColor, srgbCooked));
    const uint8_t* srgbMip1 = srgbCooked.Blob.data() + srgbCooked.Mips[1].Offset;
    EXPECT_GE(srgbMip1[0], 186);
    EXPECT_LE(srgbMip1[0], 190);

    TextureData linearCooked;
    ASSERT_TRUE(BuildTextureMipChainRgba8(checker, TextureUsage::LinearData, linearCooked));
    EXPECT_EQ(linearCooked.Format, TexturePixelFormat::RGBA8);
    const uint8_t* linearMip1 = linearCooked.Blob.data() + linearCooked.Mips[1].Offset;
    EXPECT_GE(linearMip1[0], 127);
    EXPECT_LE(linearMip1[0], 128);
}

TEST(TextureCook, NormalMipsRenormalize)
{
    // Normals tilted ±45° in X average to (0, 0, 0.707) — shorter than unit.
    // The cook must renormalize to (0, 0, 1): byte z snaps to 255 where a
    // naive byte average would leave ~218 (a visibly flattened normal).
    Image normals = MakeImage(2, 2, PixelFormat::RGBA8);
    for (uint32_t i = 0; i < 4; ++i)
    {
        uint8_t* p = normals.Pixels.data() + i * 4;
        p[0] = (i % 2 == 0) ? 218 : 37; // +0.71 / -0.71 in X
        p[1] = 128;
        p[2] = 218;                     // +0.71 in Z
        p[3] = 255;
    }

    TextureData cooked;
    ASSERT_TRUE(BuildTextureMipChainRgba8(normals, TextureUsage::Normal, cooked));
    const uint8_t* mip1 = cooked.Blob.data() + cooked.Mips[1].Offset;
    EXPECT_NEAR(mip1[0], 128, 1);
    EXPECT_NEAR(mip1[1], 128, 2);
    EXPECT_GE(mip1[2], 250);
}

TEST(TextureCook, UsageInferredFromStemSuffix)
{
    EXPECT_EQ(InferTextureUsageFromName("textures/wall_n.png"), TextureUsage::Normal);
    EXPECT_EQ(InferTextureUsageFromName("textures/wall_normal.png"), TextureUsage::Normal);
    EXPECT_EQ(InferTextureUsageFromName("a/b/c/wall_orm.png"), TextureUsage::Orm);
    EXPECT_EQ(InferTextureUsageFromName("glow_emissive.png"), TextureUsage::Emissive);
    EXPECT_EQ(InferTextureUsageFromName("blend_mask.png"), TextureUsage::LinearData);
    EXPECT_EQ(InferTextureUsageFromName("textures/wall.png"), TextureUsage::BaseColor);
    EXPECT_EQ(InferTextureUsageFromName("normal/wall.png"), TextureUsage::BaseColor);
}

TEST(TextureCook, CookedFormatsFollowTheDecisionLTable)
{
    EXPECT_EQ(CookedFormatForUsage(TextureUsage::BaseColor), TexturePixelFormat::BC7_SRGB);
    EXPECT_EQ(CookedFormatForUsage(TextureUsage::Emissive), TexturePixelFormat::BC7_SRGB);
    EXPECT_EQ(CookedFormatForUsage(TextureUsage::Normal), TexturePixelFormat::BC5);
    EXPECT_EQ(CookedFormatForUsage(TextureUsage::Orm), TexturePixelFormat::BC7);
    EXPECT_EQ(CookedFormatForUsage(TextureUsage::LinearData), TexturePixelFormat::BC7);
}

TEST(TextureCook, CookCompressesFullChainsToTaggedFormats)
{
    TextureData baseColor;
    ASSERT_TRUE(CookImageToTexture(MakeCheckerImage(16), TextureUsage::BaseColor, baseColor));
    EXPECT_EQ(baseColor.Format, TexturePixelFormat::BC7_SRGB);
    EXPECT_EQ(baseColor.Mips.size(), 5u);
    EXPECT_TRUE(ValidateTextureData(baseColor));
    // 16x16 BC7 = 4x4 blocks of 16 bytes.
    EXPECT_EQ(baseColor.Mips[0].ByteSize, 16u * 16u);

    TextureData normal;
    ASSERT_TRUE(CookImageToTexture(MakeImage(8, 8, PixelFormat::RGBA8),
                                   TextureUsage::Normal, normal));
    EXPECT_EQ(normal.Format, TexturePixelFormat::BC5);
    EXPECT_TRUE(ValidateTextureData(normal));

    TextureData orm;
    ASSERT_TRUE(CookImageToTexture(MakeImage(8, 8, PixelFormat::RGBA8),
                                   TextureUsage::Orm, orm));
    EXPECT_EQ(orm.Format, TexturePixelFormat::BC7);
    EXPECT_TRUE(ValidateTextureData(orm));
}

TEST(TextureCook, Bc7BlocksDecodeBackToTheSourceColor)
{
    // Encoder sanity, not container plumbing: a solid color must survive
    // BC7 compression essentially exactly.
    Image solid = MakeImage(4, 4);
    for (uint32_t i = 0; i < 16; ++i)
    {
        uint8_t* p = solid.Pixels.data() + i * 4;
        p[0] = 200; p[1] = 64; p[2] = 32; p[3] = 255;
    }

    TextureData cooked;
    ASSERT_TRUE(CookImageToTexture(solid, TextureUsage::Orm, cooked));
    ASSERT_EQ(cooked.Format, TexturePixelFormat::BC7);
    ASSERT_GE(cooked.Blob.size(), 16u);

    bc7decomp::color_rgba decoded[16];
    ASSERT_TRUE(bc7decomp::unpack_bc7(cooked.Blob.data(), decoded));
    for (const bc7decomp::color_rgba& texel : decoded)
    {
        EXPECT_NEAR(texel.r, 200, 8);
        EXPECT_NEAR(texel.g, 64, 8);
        EXPECT_NEAR(texel.b, 32, 8);
        EXPECT_NEAR(texel.a, 255, 1);
    }
}

#endif // SENCHA_ENABLE_COOK

// -- TextureAssetLoader sniffing ------------------------------------------------------

namespace
{
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

    private:
        std::map<std::string, std::vector<std::byte>> Files;
    };

    AssetRecord MakeTextureRecord(std::string_view path)
    {
        return AssetRecord{
            .Type = AssetType::Texture,
            .SourceKind = AssetSourceKind::File,
            .Path = std::string(path),
            .FilePath = std::string(path),
        };
    }
}

TEST(TextureAssetLoaderStex, SniffsContainerOverExtension)
{
    LoggingProvider logging;
    TextureAssetLoader loader(logging, nullptr);

    // .stex bytes registered under a ".png" virtual path — exactly what a
    // cooked artifact looks like (the artifact keeps the source's path).
    const TextureData fixture = MakeBlockCompressedFixture(TexturePixelFormat::BC7_SRGB, 8);
    std::vector<std::byte> stexBytes;
    ASSERT_TRUE(WriteStexToBytes(fixture, stexBytes));

    MemoryAssetSource source;
    source.Add("asset://textures/dev/cooked.png", std::move(stexBytes));

    AssetStaging staging = loader.LoadStaged(
        MakeTextureRecord("asset://textures/dev/cooked.png"), source);
    ASSERT_TRUE(staging.IsValid()) << staging.Error;

    const auto* texture = std::any_cast<TextureData>(&staging.Payload);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->Format, TexturePixelFormat::BC7_SRGB);
    EXPECT_EQ(texture->Mips.size(), fixture.Mips.size());

    // No cache: commit fails cleanly.
    EXPECT_FALSE(loader.CommitTyped(std::move(staging)).IsValid());
}

TEST(TextureAssetLoaderStex, CorruptStexFailsStagingWithError)
{
    LoggingProvider logging;
    TextureAssetLoader loader(logging, nullptr);

    const TextureData fixture = MakeBlockCompressedFixture(TexturePixelFormat::BC7, 8);
    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteStexToBytes(fixture, bytes));
    bytes.resize(bytes.size() / 2); // truncate past the header

    MemoryAssetSource source;
    source.Add("asset://textures/dev/torn.stex", std::move(bytes));

    AssetStaging staging = loader.LoadStaged(
        MakeTextureRecord("asset://textures/dev/torn.stex"), source);
    EXPECT_FALSE(staging.IsValid());
    EXPECT_NE(staging.Error.find("stex"), std::string::npos);
}

#ifdef SENCHA_ENABLE_COOK

// -- PNG -> .stex, end to end through import-on-demand --------------------------------

namespace
{
    // 16x16 grayscale checker PNG (the CubeDemo fixture, embedded).
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

    class TempTextureRoot
    {
    public:
        TempTextureRoot()
        {
            std::random_device rd;
            Root = std::filesystem::temp_directory_path()
                 / ("sencha_texture_import_test_" + std::to_string(rd()));
            std::filesystem::create_directories(Root / "textures/dev");
            std::ofstream png(Root / "textures/dev/checker.png",
                              std::ios::binary | std::ios::trunc);
            png.write(reinterpret_cast<const char*>(kCheckerPng), sizeof(kCheckerPng));
        }

        ~TempTextureRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        std::filesystem::path Root;
    };
}

TEST(PngTextureImport, EndToEndThroughImportOnDemand)
{
    TempTextureRoot root;
    LoggingProvider logging;
    AssetRegistry registry(logging);

    PngTextureImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));

    ImportOnDemandStats stats;
    ASSERT_TRUE(ImportAssetsOnDemand(root.Root.generic_string(), importers, registry,
                                     logging, &stats));
    EXPECT_EQ(stats.SourcesSeen, 1u);
    EXPECT_EQ(stats.Imported, 1u);
    EXPECT_EQ(stats.Failed, 0u);

    // The artifact registered under the source's virtual path.
    const AssetRecord* record = registry.FindByPath("asset://textures/dev/checker.png");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Type, AssetType::Texture);
    EXPECT_TRUE(record->FilePath.ends_with(".cooked/textures/dev/checker.png.stex"));

    // The artifact is a well-formed usage-tagged .stex with a full mip chain.
    FileAssetSource files;
    std::vector<std::byte> bytes;
    ASSERT_TRUE(files.ReadBytes(record->FilePath, bytes));
    ASSERT_TRUE(LooksLikeStex(bytes.data(), bytes.size()));

    TextureData texture;
    std::string error;
    ASSERT_TRUE(LoadStexFromBytes(bytes, texture, &error)) << error;
    EXPECT_EQ(texture.Width, 16u);
    EXPECT_EQ(texture.Height, 16u);
    EXPECT_EQ(texture.Mips.size(), 5u);
    EXPECT_EQ(texture.Format, TexturePixelFormat::BC7_SRGB);
    EXPECT_EQ(texture.Usage, TextureUsage::BaseColor);

    // Warm cache: the importer is not invoked again.
    AssetRegistry registry2(logging);
    ImportOnDemandStats stats2;
    ASSERT_TRUE(ImportAssetsOnDemand(root.Root.generic_string(), importers, registry2,
                                     logging, &stats2));
    EXPECT_EQ(stats2.CookedFresh, 1u);
    EXPECT_EQ(stats2.Imported, 0u);
    EXPECT_TRUE(registry2.Contains("asset://textures/dev/checker.png"));
}

TEST(PngTextureImport, SuffixedSourcesCookToTaggedFormats)
{
    // The Stage 4 gate clause: a normal-map fixture cooks to its tagged
    // format (BC5) and round-trips the texture path.
    TempTextureRoot root;
    std::filesystem::rename(root.Root / "textures/dev/checker.png",
                            root.Root / "textures/dev/wall_n.png");

    LoggingProvider logging;
    AssetRegistry registry(logging);
    PngTextureImporter importer;
    AssetImporterRegistry importers;
    ASSERT_TRUE(importers.Register(importer));
    ASSERT_TRUE(ImportAssetsOnDemand(root.Root.generic_string(), importers, registry, logging));

    const AssetRecord* record = registry.FindByPath("asset://textures/dev/wall_n.png");
    ASSERT_NE(record, nullptr);

    FileAssetSource files;
    std::vector<std::byte> bytes;
    ASSERT_TRUE(files.ReadBytes(record->FilePath, bytes));

    TextureData texture;
    std::string error;
    ASSERT_TRUE(LoadStexFromBytes(bytes, texture, &error)) << error;
    EXPECT_EQ(texture.Format, TexturePixelFormat::BC5);
    EXPECT_EQ(texture.Usage, TextureUsage::Normal);
    EXPECT_EQ(texture.Mips.size(), 5u);
}

#endif // SENCHA_ENABLE_COOK

// -- Import settings (sidecar) ------------------------------------------------

namespace
{
    std::span<const std::byte> AsBytes(std::string_view text)
    {
        return std::as_bytes(std::span(text.data(), text.size()));
    }
}

TEST(TextureImportSettings, EmptyInputIsDefaults)
{
    TextureImportSettings settings;
    settings.Filter = TextureFilter::Nearest; // must be overwritten
    ASSERT_TRUE(ParseTextureImportSettings({}, settings));
    EXPECT_EQ(settings, TextureImportSettings{});
}

TEST(TextureImportSettings, ParsesEveryField)
{
    TextureImportSettings settings;
    std::string error;
    ASSERT_TRUE(ParseTextureImportSettings(AsBytes(
        R"({"version": 1, "usage": "normal", "filter": "nearest", "compress": false, "mips": false})"),
        settings, &error)) << error;
    EXPECT_EQ(settings.Usage, TextureUsage::Normal);
    EXPECT_EQ(settings.Filter, TextureFilter::Nearest);
    EXPECT_FALSE(settings.Compress);
    EXPECT_FALSE(settings.GenerateMips);
}

TEST(TextureImportSettings, RejectsTyposInsteadOfDefaulting)
{
    TextureImportSettings settings;
    std::string error;
    EXPECT_FALSE(ParseTextureImportSettings(AsBytes(R"({"filtr": "nearest"})"), settings, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(ParseTextureImportSettings(AsBytes(R"({"filter": "nearset"})"), settings, &error));
    EXPECT_FALSE(ParseTextureImportSettings(AsBytes("not json"), settings, &error));
}

TEST(TextureImportSettings, SaveFileRoundTrips)
{
    TextureImportSettings settings;
    settings.Usage = TextureUsage::LinearData;
    settings.Filter = TextureFilter::Nearest;
    settings.Compress = false;
    settings.GenerateMips = false;

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "sencha_import_settings_roundtrip.meta";
    std::string error;
    ASSERT_TRUE(SaveTextureImportSettingsFile(path.string(), settings, &error)) << error;

    std::ifstream file(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), {});
    TextureImportSettings loaded;
    ASSERT_TRUE(ParseTextureImportSettings(AsBytes(text), loaded, &error)) << error;
    EXPECT_EQ(loaded, settings);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// -- Cook params (uncompressed / no-mips / filter) ------------------------------

TEST(TextureCook, UncompressedParamsKeepRgbaAndColorspace)
{
    TextureData texture;
    ASSERT_TRUE(CookImageToTexture(
        MakeImage(8, 8),
        TextureCookParams{ .Usage = TextureUsage::BaseColor, .Compress = false }, texture));
    EXPECT_EQ(texture.Format, TexturePixelFormat::RGBA8_SRGB);
    EXPECT_EQ(texture.Mips.size(), FullMipChainLength(8, 8));
    ASSERT_TRUE(ValidateTextureData(texture));

    ASSERT_TRUE(CookImageToTexture(
        MakeImage(8, 8, PixelFormat::RGBA8),
        TextureCookParams{ .Usage = TextureUsage::LinearData, .Compress = false }, texture));
    EXPECT_EQ(texture.Format, TexturePixelFormat::RGBA8);
}

TEST(TextureCook, NoMipsParamsEmitOnlyMipZero)
{
    TextureData texture;
    ASSERT_TRUE(CookImageToTexture(
        MakeImage(16, 16),
        TextureCookParams{ .Usage = TextureUsage::BaseColor,
                           .Compress = false,
                           .GenerateMips = false },
        texture));
    ASSERT_EQ(texture.Mips.size(), 1u);
    EXPECT_EQ(texture.Mips[0].Width, 16u);
    ASSERT_TRUE(ValidateTextureData(texture));
}

TEST(TextureCook, FilterRidesTheStexRoundTrip)
{
    TextureData texture;
    ASSERT_TRUE(CookImageToTexture(
        MakeImage(8, 8),
        TextureCookParams{ .Usage = TextureUsage::BaseColor,
                           .Filter = TextureFilter::Nearest },
        texture));
    EXPECT_EQ(texture.Filter, TextureFilter::Nearest);

    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteStexToBytes(texture, bytes));
    TextureData loaded;
    std::string error;
    ASSERT_TRUE(LoadStexFromBytes(bytes, loaded, &error)) << error;
    EXPECT_EQ(loaded.Filter, TextureFilter::Nearest);

    // Default-filtered textures round-trip as linear (flag absent).
    ASSERT_TRUE(CookImageToTexture(MakeImage(8, 8), TextureUsage::BaseColor, texture));
    ASSERT_TRUE(WriteStexToBytes(texture, bytes));
    ASSERT_TRUE(LoadStexFromBytes(bytes, loaded, &error)) << error;
    EXPECT_EQ(loaded.Filter, TextureFilter::Linear);
}
