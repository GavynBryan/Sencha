#include <assets/cook/MeshImportSettings.h>
#include <assets/cook/TextureImportSettings.h>
#include <assets/material/MaterialLoader.h>
#include <assets/material/MaterialWriter.h>
#include <core/assets/AssetIdentity.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <string>

// Where an asset states its id, and every format that holds one keeping it:
// a document at its root, any other file in its sidecar beside its settings.
namespace
{
const AssetId kId{ 0x00000000c0ffee00ull };

std::span<const std::byte> Bytes(const std::string& text)
{
    return { reinterpret_cast<const std::byte*>(text.data()), text.size() };
}
} // namespace

TEST(AssetIdentity, DocumentsStateTheirIdInsideEverythingElseBeside)
{
    EXPECT_TRUE(StatesIdInside(".smat"));
    EXPECT_TRUE(StatesIdInside(".sscene"));
    EXPECT_TRUE(StatesIdInside(".sdata"));
    EXPECT_FALSE(StatesIdInside(".png"));
    EXPECT_EQ(IdStatementOf("a/stone.smat"), std::filesystem::path("a/stone.smat"));
    EXPECT_EQ(IdStatementOf("a/stone.png"), std::filesystem::path("a/stone.png.meta"));
}

TEST(AssetIdentity, ACopyForgetsTheIdItWasCopiedWith)
{
    std::optional<JsonValue> root = JsonParse(R"({"id": "00000000c0ffee00", "type": "game.settings"})");
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(StatedAssetId(*root), kId);
    ForgetStatedAssetId(*root);
    EXPECT_FALSE(StatedAssetId(*root).has_value());
    EXPECT_NE(root->Find("type"), nullptr);
}

TEST(AssetIdentity, AFileStatesItsIdInsideOrInItsSidecar)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("sencha_identity_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "stone.smat") << R"({"id": "00000000c0ffee00", "version": 2})";
    std::ofstream(dir / "stone.png") << "png";
    std::ofstream(dir / "stone.png.meta") << R"({"id": "00000000c0ffee00", "version": 1})";
    std::ofstream(dir / "bare.png") << "png";

    EXPECT_EQ(ReadStatedAssetId(dir / "stone.smat"), kId);
    EXPECT_EQ(ReadStatedAssetId(dir / "stone.png"), kId);
    EXPECT_FALSE(ReadStatedAssetId(dir / "bare.png").has_value());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(AssetIdentity, AMaterialKeepsItsIdThroughASave)
{
    MaterialDescription description;
    description.Id = kId;
    MaterialDescription read;
    ASSERT_TRUE(ParseMaterialJson(WriteMaterialJson(description), read));
    EXPECT_EQ(read.Id, kId);

    std::optional<JsonValue> malformed = JsonParse(R"({"id": "C0FFEE", "version": 2})");
    ASSERT_TRUE(malformed.has_value());
    EXPECT_FALSE(ParseMaterialJson(*malformed, read));
}

TEST(AssetIdentity, ASidecarKeepsItsIdBesideItsSettings)
{
    TextureImportSettings texture;
    ASSERT_TRUE(ParseTextureImportSettings(Bytes(R"({"id": "00000000c0ffee00", "version": 1, "mips": false})"), texture));
    EXPECT_FALSE(texture.GenerateMips);

    const std::filesystem::path sidecar =
        std::filesystem::temp_directory_path() / ("sencha_sidecar_" + std::to_string(std::random_device{}()) + ".png.meta");
    ASSERT_TRUE(SaveTextureImportSettingsFile(sidecar.string(), texture, kId));
    std::ifstream written(sidecar);
    const std::optional<JsonValue> root = JsonParse(std::string(std::istreambuf_iterator<char>(written), {}));
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(StatedAssetId(*root), kId);
    std::filesystem::remove(sidecar);

    MeshImportSettings mesh;
    const std::string text = WriteMeshImportSettings(mesh, kId);
    MeshImportSettings reread;
    ASSERT_TRUE(ParseMeshImportSettings(Bytes(text), reread));
    EXPECT_EQ(StatedAssetId(*JsonParse(text)), kId);
}
