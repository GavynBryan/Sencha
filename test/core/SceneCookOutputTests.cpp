#include <assets/cook/SceneCookOutput.h>

#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetManifest.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    // A scratch assets root that maps asset://x to <root>/x, like the flat cook.
    class SceneCookOutputTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Root = fs::temp_directory_path()
                / ("sencha_scenecook_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed())
                   + "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
            fs::remove_all(Root);
            fs::create_directories(Root);
        }

        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }

        void WriteFile(std::string_view assetPath, std::string_view contents)
        {
            const fs::path p = Physical(assetPath);
            fs::create_directories(p.parent_path());
            std::ofstream(p, std::ios::trunc) << contents;
        }

        [[nodiscard]] fs::path Physical(std::string_view assetPath) const
        {
            constexpr std::string_view prefix = "asset://";
            return Root / std::string(assetPath.substr(prefix.size()));
        }

        [[nodiscard]] std::function<fs::path(std::string_view)> Resolver() const
        {
            return [this](std::string_view p) { return Physical(p); };
        }

        [[nodiscard]] JsonValue ReadJson(const fs::path& p) const
        {
            std::ifstream f(p);
            std::ostringstream buf;
            buf << f.rdbuf();
            return *JsonParse(buf.str());
        }

        fs::path Root;
    };

    // Minimal cooked scene referencing one mesh and one material.
    JsonValue SceneWith(const std::string& mesh, const std::string& material)
    {
        JsonValue::Object staticMesh{
            { "mesh", JsonValue(mesh) },
            { "materials", JsonValue(JsonValue::Array{ JsonValue(material) }) },
        };
        JsonValue::Object components{ { "StaticMesh", JsonValue(std::move(staticMesh)) } };
        JsonValue::Object entity{ { "components", JsonValue(std::move(components)) } };
        return JsonValue(JsonValue::Object{
            { "version", JsonValue(1.0) },
            { "entities", JsonValue(JsonValue::Array{ JsonValue(std::move(entity)) }) },
        });
    }
}

TEST_F(SceneCookOutputTest, ManifestCoversSceneRefsSmatIndirectionAndExtraRefs)
{
    WriteFile("asset://meshes/cell.smesh", "smesh-bytes");
    WriteFile("asset://materials/gray.smat", R"({"albedo":"asset://textures/gray.stex"})");
    WriteFile("asset://materials/brick.smat", R"({"albedo":"asset://textures/brick.stex"})");
    WriteFile("asset://textures/gray.stex", "stex-bytes");
    WriteFile("asset://textures/brick.stex", "stex-bytes");

    const JsonValue scene = SceneWith("asset://meshes/cell.smesh", "asset://materials/gray.smat");
    // brick.smat is real (a sidecar face material) but never appears in the scene
    // JSON; it must still ride into the manifest and pull its own texture.
    const std::vector<std::string> extraRefs = { "asset://materials/brick.smat" };

    std::string error;
    ASSERT_TRUE(WriteCookedScene(scene, extraRefs, Resolver(),
        Root / "asset_ids.json", Root / "scene.manifest.json", Root / "scene.cooked.json",
        &error)) << error;

    AssetManifest manifest;
    ASSERT_TRUE(LoadAssetManifestFile((Root / "scene.manifest.json").generic_string(), manifest));

    std::vector<std::string> got;
    for (const AssetManifestEntry& e : manifest.Entries)
        got.push_back(e.Path);

    auto has = [&](const std::string& p) {
        return std::find(got.begin(), got.end(), p) != got.end();
    };
    EXPECT_TRUE(has("asset://meshes/cell.smesh"));
    EXPECT_TRUE(has("asset://materials/gray.smat"));
    EXPECT_TRUE(has("asset://textures/gray.stex"));   // gray.smat indirection
    EXPECT_TRUE(has("asset://materials/brick.smat"));  // extra ref
    EXPECT_TRUE(has("asset://textures/brick.stex"));   // brick.smat indirection
    EXPECT_EQ(got.size(), 5u);
}

TEST_F(SceneCookOutputTest, CookedSceneStampsKnownRefs)
{
    WriteFile("asset://meshes/cell.smesh", "smesh-bytes");
    WriteFile("asset://materials/gray.smat", R"({})");

    const JsonValue scene = SceneWith("asset://meshes/cell.smesh", "asset://materials/gray.smat");
    std::string error;
    ASSERT_TRUE(WriteCookedScene(scene, {}, Resolver(),
        Root / "asset_ids.json", Root / "scene.manifest.json", Root / "scene.cooked.json",
        &error)) << error;

    const JsonValue cooked = ReadJson(Root / "scene.cooked.json");
    const JsonValue& staticMesh =
        cooked.AsObject().at(1).second  // "entities"
            .AsArray().at(0)
            .AsObject().at(0).second     // "components"
            .AsObject().at(0).second;    // "StaticMesh"

    // The mesh ref is stamped from a bare string to {"id","path"}.
    const JsonValue& mesh = staticMesh.AsObject().at(0).second; // "mesh"
    ASSERT_TRUE(mesh.IsObject());
    EXPECT_TRUE(mesh.AsObject().at(0).first == "id" || mesh.AsObject().at(1).first == "id");
}

TEST_F(SceneCookOutputTest, IdsAreStableAcrossRecook)
{
    WriteFile("asset://meshes/cell.smesh", "smesh-bytes");
    const JsonValue scene = SceneWith("asset://meshes/cell.smesh", "asset://materials/gray.smat");
    WriteFile("asset://materials/gray.smat", R"({})");

    const auto cook = [&] {
        std::string error;
        return WriteCookedScene(scene, {}, Resolver(),
            Root / "asset_ids.json", Root / "scene.manifest.json", Root / "scene.cooked.json",
            &error);
    };

    ASSERT_TRUE(cook());
    AssetIdMap first;
    ASSERT_TRUE(AssetIdMap::LoadFromFile((Root / "asset_ids.json").generic_string(), first));

    ASSERT_TRUE(cook());
    AssetIdMap second;
    ASSERT_TRUE(AssetIdMap::LoadFromFile((Root / "asset_ids.json").generic_string(), second));

    EXPECT_EQ(first.FindId("asset://meshes/cell.smesh"),
              second.FindId("asset://meshes/cell.smesh"));
    EXPECT_TRUE(first.FindId("asset://meshes/cell.smesh").IsValid());
}
