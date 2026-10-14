#include <assets/cook/SceneCookOutput.h>

#include <core/assets/AssetId.h>
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
                Serializers, Resolver(), Root / "scene.smap", error);
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

// A dependency goes by the id its reference names, else the one its file
// states, else its path's; the scene's references pass through as written.
TEST_F(SceneCookOutputTest, DependenciesGoByTheIdsTheAssetsGoBy)
{
    WriteFile("asset://meshes/cell.smesh", "smesh-bytes");
    WriteFile("asset://meshes/cell.smesh.meta", R"({"id": "00000000000000c1"})");
    WriteFile("asset://materials/gray.smat", R"({"id": "00000000000000c2", "version": 2})");
    WriteFile("asset://materials/brick.smat", R"({"version": 2})");

    JsonValue scene = SceneWith("asset://meshes/cell.smesh", "asset://materials/gray.smat");
    const std::vector<std::string> extraRefs = { "asset://materials/brick.smat" };
    std::string error;
    ASSERT_TRUE(Cook(scene, extraRefs, &error)) << error;

    SmapContents contents;
    SmapError readError;
    ASSERT_TRUE(ReadSmapFile(Root / "scene.smap", Serializers, contents, &readError)) << readError.Message;
    const auto idOf = [&](std::string_view path) {
        const auto found = std::ranges::find(contents.Dependencies, path, &SmapDependency::Path);
        return found != contents.Dependencies.end() ? found->Id : AssetId{};
    };
    EXPECT_EQ(idOf("asset://meshes/cell.smesh"), AssetId{ 0xc1 }) << "its sidecar states it";
    EXPECT_EQ(idOf("asset://materials/gray.smat"), AssetId{ 0xc2 }) << "its document states it";
    EXPECT_EQ(idOf("asset://materials/brick.smat"), AssetIdOfPath("asset://materials/brick.smat"));

    const JsonValue& staticMesh = contents.Entities[0].Components[0].second;
    EXPECT_TRUE(staticMesh.Find("mesh")->IsString()) << "a bare path is cooked as written";
}

TEST_F(SceneCookOutputTest, AReferenceByIdNamesItsDependencyByThatId)
{
    WriteFile("asset://meshes/cell.smesh", "smesh-bytes");
    WriteFile("asset://materials/gray.smat", R"({"version": 2})");
    JsonValue::Object mesh{ { "id", JsonValue("00000000000000d4") }, { "path", JsonValue("asset://meshes/cell.smesh") } };
    JsonValue::Object staticMesh{ { "mesh", JsonValue(std::move(mesh)) },
                                  { "materials", JsonValue(JsonValue::Array{ JsonValue("asset://materials/gray.smat") }) } };
    JsonValue::Object components{ { "StaticMesh", JsonValue(std::move(staticMesh)) } };
    JsonValue::Object entity{ { "components", JsonValue(std::move(components)) } };
    const JsonValue scene(JsonValue::Object{ { "version", JsonValue(1.0) },
                                             { "entities", JsonValue(JsonValue::Array{ JsonValue(std::move(entity)) }) } });

    std::string error;
    ASSERT_TRUE(Cook(scene, {}, &error)) << error;
    SmapContents metadata;
    SmapError metadataError;
    ASSERT_TRUE(ReadSmapMetadataFile(Root / "scene.smap", metadata, &metadataError)) << metadataError.Message;
    const auto cell = std::ranges::find(metadata.Dependencies, std::string_view("asset://meshes/cell.smesh"), &SmapDependency::Path);
    ASSERT_NE(cell, metadata.Dependencies.end());
    EXPECT_EQ(cell->Id, AssetId{ 0xd4 });
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
