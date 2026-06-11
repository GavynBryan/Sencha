#include <assets/material/MaterialLoader.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <core/handle/ILifetimeOwner.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <render/MaterialCache.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace
{
    JsonValue ParseOrDie(std::string_view text)
    {
        std::optional<JsonValue> json = JsonParse(std::string(text));
        EXPECT_TRUE(json.has_value());
        return json.value_or(JsonValue{});
    }

    // Stand-in for TextureCache on the far side of TextureCacheHandle:
    // counts lifetime traffic so the material→texture release chain can be
    // asserted without a GPU.
    class CountingLifetimeOwner final : public ILifetimeOwner
    {
    public:
        void Attach(uint64_t) override { ++Attached; }
        void Detach(uint64_t) override { ++Detached; }

        int Attached = 0;
        int Detached = 0;
    };

    class TempFile
    {
    public:
        explicit TempFile(std::string_view contents)
        {
            static int counter = 0;
            Path = std::filesystem::temp_directory_path() /
                   ("sencha_material_test_" + std::to_string(++counter) + ".smat");
            std::ofstream file(Path, std::ios::trunc);
            file << contents;
        }

        ~TempFile()
        {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }

        std::filesystem::path Path;
    };
} // namespace

// -- ParseMaterialJson --------------------------------------------------------

TEST(MaterialLoader, ParsesFullSchema)
{
    const JsonValue json = ParseOrDie(R"({
        "version": 1,
        "base_color_factor": [0.5, 0.25, 0.75, 1.0],
        "base_color_texture": "asset://textures/dev/base.png",
        "normal_texture": "asset://textures/dev/normal.png",
        "normal_scale": 0.8,
        "orm_texture": "asset://textures/dev/orm.png",
        "roughness_factor": 0.4,
        "metallic_factor": 0.9,
        "emissive_factor": [0.1, 0.2, 0.3],
        "emissive_texture": "asset://textures/dev/glow.png",
        "alpha_mode": "mask",
        "alpha_cutoff": 0.33
    })");

    MaterialDescription desc;
    MaterialParseError error;
    ASSERT_TRUE(ParseMaterialJson(json, desc, &error)) << error.Message;

    EXPECT_FLOAT_EQ(desc.BaseColorFactor.X, 0.5f);
    EXPECT_FLOAT_EQ(desc.BaseColorFactor.Y, 0.25f);
    EXPECT_FLOAT_EQ(desc.BaseColorFactor.Z, 0.75f);
    EXPECT_FLOAT_EQ(desc.BaseColorFactor.W, 1.0f);
    EXPECT_EQ(desc.BaseColorTexture.Type, AssetType::Texture);
    EXPECT_EQ(desc.BaseColorTexture.Path, "asset://textures/dev/base.png");
    EXPECT_EQ(desc.NormalTexture.Path, "asset://textures/dev/normal.png");
    EXPECT_FLOAT_EQ(desc.NormalScale, 0.8f);
    EXPECT_EQ(desc.OrmTexture.Path, "asset://textures/dev/orm.png");
    EXPECT_FLOAT_EQ(desc.RoughnessFactor, 0.4f);
    EXPECT_FLOAT_EQ(desc.MetallicFactor, 0.9f);
    EXPECT_FLOAT_EQ(desc.EmissiveFactor.X, 0.1f);
    EXPECT_FLOAT_EQ(desc.EmissiveFactor.Y, 0.2f);
    EXPECT_FLOAT_EQ(desc.EmissiveFactor.Z, 0.3f);
    EXPECT_EQ(desc.EmissiveTexture.Path, "asset://textures/dev/glow.png");
    EXPECT_EQ(desc.AlphaMode, MaterialAlphaMode::Mask);
    EXPECT_FLOAT_EQ(desc.AlphaCutoff, 0.33f);
}

TEST(MaterialLoader, VersionOnlyYieldsNeutralDefaults)
{
    MaterialDescription desc;
    ASSERT_TRUE(ParseMaterialJson(ParseOrDie(R"({"version": 1})"), desc));

    EXPECT_FLOAT_EQ(desc.BaseColorFactor.X, 1.0f);
    EXPECT_FLOAT_EQ(desc.BaseColorFactor.W, 1.0f);
    EXPECT_FALSE(desc.BaseColorTexture.IsValid());
    EXPECT_FALSE(desc.NormalTexture.IsValid());
    EXPECT_FALSE(desc.OrmTexture.IsValid());
    EXPECT_FALSE(desc.EmissiveTexture.IsValid());
    EXPECT_FLOAT_EQ(desc.NormalScale, 1.0f);
    EXPECT_FLOAT_EQ(desc.RoughnessFactor, 1.0f);
    EXPECT_FLOAT_EQ(desc.MetallicFactor, 0.0f);
    EXPECT_EQ(desc.AlphaMode, MaterialAlphaMode::Opaque);
    EXPECT_FLOAT_EQ(desc.AlphaCutoff, 0.5f);
}

TEST(MaterialLoader, RejectsUnknownKey)
{
    MaterialDescription desc;
    MaterialParseError error;
    EXPECT_FALSE(ParseMaterialJson(
        ParseOrDie(R"({"version": 1, "base_colour_factor": [1, 1, 1, 1]})"), desc, &error));
    EXPECT_NE(error.Message.find("base_colour_factor"), std::string::npos);
}

TEST(MaterialLoader, RejectsMissingOrWrongVersion)
{
    MaterialDescription desc;
    EXPECT_FALSE(ParseMaterialJson(ParseOrDie(R"({})"), desc));
    EXPECT_FALSE(ParseMaterialJson(ParseOrDie(R"({"version": 2})"), desc));
}

TEST(MaterialLoader, RejectsWrongFactorArity)
{
    MaterialDescription desc;
    EXPECT_FALSE(ParseMaterialJson(
        ParseOrDie(R"({"version": 1, "base_color_factor": [1, 1, 1]})"), desc));
    EXPECT_FALSE(ParseMaterialJson(
        ParseOrDie(R"({"version": 1, "emissive_factor": [1, 1, 1, 1]})"), desc));
}

TEST(MaterialLoader, RejectsInvalidTexturePathAndAlphaMode)
{
    MaterialDescription desc;
    EXPECT_FALSE(ParseMaterialJson(
        ParseOrDie(R"({"version": 1, "base_color_texture": "textures/no_prefix.png"})"), desc));
    EXPECT_FALSE(ParseMaterialJson(
        ParseOrDie(R"({"version": 1, "alpha_mode": "translucent"})"), desc));
}

TEST(MaterialLoader, LoadFromFileReportsMissingFile)
{
    MaterialDescription desc;
    MaterialParseError error;
    EXPECT_FALSE(LoadMaterialFromFile("does/not/exist.smat", desc, &error));
    EXPECT_FALSE(error.Message.empty());
}

// -- MaterialCache owned-texture lifetime --------------------------------------

TEST(MaterialCacheOwnership, ReleasingMaterialReleasesOwnedTextures)
{
    CountingLifetimeOwner textures;
    MaterialCache materials;

    std::vector<TextureCacheHandle> owned;
    owned.emplace_back(&textures, TextureHandle{ 1 }, TextureCacheHandle::NoAttach);
    owned.emplace_back(&textures, TextureHandle{ 2 }, TextureCacheHandle::NoAttach);

    MaterialHandle handle =
        materials.Register("asset://materials/test/owned.smat", Material{}, std::move(owned));
    ASSERT_TRUE(handle.IsValid());
    EXPECT_EQ(textures.Detached, 0);

    materials.Destroy(handle);
    EXPECT_EQ(textures.Detached, 2);
}

TEST(MaterialCacheOwnership, DuplicateRegisterReleasesIncomingTextures)
{
    CountingLifetimeOwner textures;
    MaterialCache materials;

    MaterialHandle first = materials.Register("asset://materials/test/dup.smat", Material{});
    ASSERT_TRUE(first.IsValid());

    std::vector<TextureCacheHandle> owned;
    owned.emplace_back(&textures, TextureHandle{ 7 }, TextureCacheHandle::NoAttach);

    MaterialHandle second =
        materials.Register("asset://materials/test/dup.smat", Material{}, std::move(owned));
    EXPECT_EQ(second, first);
    EXPECT_EQ(textures.Detached, 1);
}

TEST(MaterialCacheOwnership, CacheDestructionReleasesOwnedTextures)
{
    CountingLifetimeOwner textures;
    {
        MaterialCache materials;
        std::vector<TextureCacheHandle> owned;
        owned.emplace_back(&textures, TextureHandle{ 3 }, TextureCacheHandle::NoAttach);
        MaterialHandle handle =
            materials.Register("asset://materials/test/leak.smat", Material{}, std::move(owned));
        ASSERT_TRUE(handle.IsValid());
    }
    EXPECT_EQ(textures.Detached, 1);
}

// -- AssetSystem .smat file loading --------------------------------------------

namespace
{
    const AssetRecord MakeFileMaterialRecord(const std::filesystem::path& filePath,
                                             std::string_view assetPath)
    {
        return AssetRecord{
            .Type = AssetType::Material,
            .SourceKind = AssetSourceKind::File,
            .Path = std::string(assetPath),
            .FilePath = filePath.generic_string(),
        };
    }
} // namespace

TEST(AssetSystemMaterial, LoadsSmatFileWithNeutralTextureSlots)
{
    const TempFile file(R"({
        "version": 1,
        "base_color_factor": [1.0, 0.15, 0.1, 1.0],
        "base_color_texture": "asset://textures/dev/checker.png",
        "alpha_mode": "blend"
    })");

    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);

    ASSERT_TRUE(registry.Register(
        MakeFileMaterialRecord(file.Path, "asset://materials/dev/test.smat")));

    MaterialHandle handle = assets.LoadMaterial("asset://materials/dev/test.smat");
    ASSERT_TRUE(handle.IsValid());

    const Material* material = materials.Get(handle);
    ASSERT_NE(material, nullptr);
    EXPECT_FLOAT_EQ(material->BaseColor.X, 1.0f);
    EXPECT_FLOAT_EQ(material->BaseColor.Y, 0.15f);
    EXPECT_FLOAT_EQ(material->BaseColor.Z, 0.1f);
    // No TextureCache: the slot stays at its neutral default rather than failing.
    EXPECT_EQ(material->BaseColorTextureIndex, UINT32_MAX);
    EXPECT_EQ(material->AlphaMode, MaterialAlphaMode::Blend);
}

TEST(AssetSystemMaterial, SecondLoadReturnsSameHandle)
{
    const TempFile file(R"({"version": 1, "base_color_factor": [0.1, 0.85, 0.45, 1.0]})");

    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);

    ASSERT_TRUE(registry.Register(
        MakeFileMaterialRecord(file.Path, "asset://materials/dev/dedup.smat")));

    MaterialHandle first = assets.LoadMaterial("asset://materials/dev/dedup.smat");
    MaterialHandle second = assets.LoadMaterial("asset://materials/dev/dedup.smat");
    ASSERT_TRUE(first.IsValid());
    EXPECT_EQ(first, second);
}

TEST(AssetSystemMaterial, MalformedSmatFailsCleanly)
{
    const TempFile file(R"({"version": 1, "unknown_key": true})");

    LoggingProvider logging;
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);

    ASSERT_TRUE(registry.Register(
        MakeFileMaterialRecord(file.Path, "asset://materials/dev/bad.smat")));

    EXPECT_FALSE(assets.LoadMaterial("asset://materials/dev/bad.smat").IsValid());
}
