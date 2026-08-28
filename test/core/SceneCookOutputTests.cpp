#include <assets/cook/SceneCookOutput.h>

#include <core/assets/AssetIdMap.h>
#include <core/json/JsonValue.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
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
            RegisterEngineSceneSerializers(Serializers);
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

        [[nodiscard]] bool Cook(const JsonValue& scene,
                                std::span<const std::string> extraRefs,
                                std::string* error)
        {
            return WriteCookedScene(scene, extraRefs, /*collisionCells*/ {},
                Serializers, Resolver(), Root / "asset_ids.json",
                Root / "scene.smap", error);
        }

        fs::path Root;
        ComponentSerializerRegistry Serializers;
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

TEST_F(SceneCookOutputTest, DependenciesCoverSceneRefsSmatIndirectionAndExtraRefs)
{
    WriteFile("asset://meshes/cell.smesh", "smesh-bytes");
    WriteFile("asset://materials/gray.smat", R"({"albedo":"asset://textures/gray.stex"})");
    WriteFile("asset://materials/brick.smat", R"({"albedo":"asset://textures/brick.stex"})");
    WriteFile("asset://textures/gray.stex", "stex-bytes");
    WriteFile("asset://textures/brick.stex", "stex-bytes");

    const JsonValue scene = SceneWith("asset://meshes/cell.smesh", "asset://materials/gray.smat");
    // brick.smat is real (a sidecar face material) but never appears in the scene
    // JSON; it must still ride into the dependency table and pull its own texture.
    const std::vector<std::string> extraRefs = { "asset://materials/brick.smat" };

    std::string error;
    ASSERT_TRUE(Cook(scene, extraRefs, &error)) << error;

    SmapContents metadata;
    SmapError metadataError;
    ASSERT_TRUE(ReadSmapMetadataFile(Root / "scene.smap", metadata, &metadataError))
        << metadataError.Message;

    std::vector<std::string> got;
    for (const SmapDependency& dependency : metadata.Dependencies)
        got.push_back(dependency.Path);

    auto has = [&](const std::string& p) {
        return std::find(got.begin(), got.end(), p) != got.end();
    };
    EXPECT_TRUE(has("asset://meshes/cell.smesh"));
    EXPECT_TRUE(has("asset://materials/gray.smat"));
    EXPECT_TRUE(has("asset://textures/gray.stex"));   // gray.smat indirection
    EXPECT_TRUE(has("asset://materials/brick.smat"));  // extra ref
    EXPECT_TRUE(has("asset://textures/brick.stex"));   // brick.smat indirection
    EXPECT_EQ(got.size(), 5u);
    for (const SmapDependency& dependency : metadata.Dependencies)
        EXPECT_TRUE(dependency.Id.IsValid()) << dependency.Path;
}

TEST_F(SceneCookOutputTest, CookedSceneStampsKnownRefs)
{
    WriteFile("asset://meshes/cell.smesh", "smesh-bytes");
    WriteFile("asset://materials/gray.smat", R"({})");

    const JsonValue scene = SceneWith("asset://meshes/cell.smesh", "asset://materials/gray.smat");
    std::string error;
    ASSERT_TRUE(Cook(scene, {}, &error)) << error;

    SmapContents contents;
    SmapError readError;
    ASSERT_TRUE(ReadSmapFile(Root / "scene.smap", Serializers, contents, &readError))
        << readError.Message;
    ASSERT_EQ(contents.Entities.size(), 1u);
    ASSERT_EQ(contents.Entities[0].Components.size(), 1u);

    // The mesh ref is stamped from a bare string to {"id","path"}.
    const JsonValue& staticMesh = contents.Entities[0].Components[0].second;
    const JsonValue* mesh = staticMesh.Find("mesh");
    ASSERT_NE(mesh, nullptr);
    ASSERT_TRUE(mesh->IsObject());
    ASSERT_NE(mesh->Find("id"), nullptr);
    ASSERT_NE(mesh->Find("path"), nullptr);
    EXPECT_EQ(mesh->Find("path")->AsString(), "asset://meshes/cell.smesh");
}

TEST_F(SceneCookOutputTest, IdsAreStableAcrossRecook)
{
    WriteFile("asset://meshes/cell.smesh", "smesh-bytes");
    const JsonValue scene = SceneWith("asset://meshes/cell.smesh", "asset://materials/gray.smat");
    WriteFile("asset://materials/gray.smat", R"({})");

    const auto cook = [&] {
        std::string error;
        return Cook(scene, {}, &error);
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

TEST_F(SceneCookOutputTest, UnknownComponentRefusesTheCook)
{
    const JsonValue scene(JsonValue::Object{
        { "version", JsonValue(1.0) },
        { "entities", JsonValue(JsonValue::Array{
            JsonValue(JsonValue::Object{
                { "components", JsonValue(JsonValue::Object{
                    { "NotARegisteredComponent", JsonValue(JsonValue::Object{}) },
                }) },
            }),
        }) },
    });

    std::string error;
    EXPECT_FALSE(Cook(scene, {}, &error));
    EXPECT_NE(error.find("NotARegisteredComponent"), std::string::npos) << error;
}
