// End-to-end: an authored brush becomes walkable collision with zero collision
// authoring. Cook a brush level -> a collision sidecar + per-cell .scol blobs ->
// LoadZoneCollision spawns static colliders -> PhysicsScene makes static bodies
// -> a downward ray hits the cooked brush. Headless: no AssetSystem, no graphics.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

#include <core/assets/AssetRef.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <physics/CollisionShapeCache.h>
#include <physics/PhysicsQueries.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsScene.h>
#include <physics/PhysicsWorld.h>
#include <physics/ZoneCollisionLoader.h>
#include <physics/components/Collider.h>
#include <physics/components/RigidBody.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
namespace fs = std::filesystem;

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

class BrushCollisionCookTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = fs::temp_directory_path()
            / ("sencha_brushcol_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(Root);
        fs::create_directories(Root);
        const fs::path material = Root / "materials/dev/gray.smat";
        fs::create_directories(material.parent_path());
        std::ofstream(material, std::ios::trunc) << "{}";
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(Root, ec);
    }

    fs::path AuthorOneBrushLevel()
    {
        EditorDocument doc(Logging);
        doc.SetDefaultMaterial(AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
        doc.GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
        const fs::path levelPath = Root / "levels/test.json";
        fs::create_directories(levelPath.parent_path());
        EXPECT_TRUE(doc.SaveAs(levelPath.generic_string()));
        return levelPath;
    }

    fs::path Root;
    LoggingProvider Logging;
};
} // namespace

TEST_F(BrushCollisionCookTest, BrushBecomesWalkableCollision)
{
    const fs::path levelPath = AuthorOneBrushLevel();
    const DocumentCookResult result = CookDocument(levelPath, Root, /*cellSize*/ 16.0);
    ASSERT_TRUE(result.Success) << result.Error;

    // The cook wrote a collision sidecar with one entry and a non-empty .scol blob.
    const fs::path sidecarPath = Root / ".cooked/levels/test.collision.json";
    ASSERT_TRUE(fs::exists(sidecarPath));
    std::ifstream sidecarFile(sidecarPath);
    std::ostringstream sidecarBuf;
    sidecarBuf << sidecarFile.rdbuf();
    const std::optional<JsonValue> sidecar = JsonParse(sidecarBuf.str());
    ASSERT_TRUE(sidecar && sidecar->IsArray());
    ASSERT_EQ(sidecar->AsArray().size(), 1u);
    const JsonValue* blob = sidecar->AsArray()[0].Find("blob");
    ASSERT_NE(blob, nullptr);
    EXPECT_GT(fs::file_size(Root / ".cooked" / blob->AsString()), 0u);

    // Runtime: load the cooked collision and bind it to a world.
    PhysicsWorld world;
    CollisionShapeCache cache;
    world.SetShapeCache(&cache);

    World ecs;
    ecs.RegisterComponent<LocalTransform>();
    RegisterPhysicsComponents(ecs);

    const int loaded = LoadZoneCollision(
        ecs, cache, sidecarPath.generic_string(), (Root / ".cooked").generic_string());
    EXPECT_EQ(loaded, 1);
    EXPECT_EQ(cache.Count(), 1u);

    // The collider entity becomes a static body, and the brush is there to hit.
    PhysicsScene scene(world);
    scene.SyncToPhysics(ecs);
    EXPECT_EQ(scene.BodyCount(), 1u);

    PhysicsQueries queries(world);
    const RaycastHit hit = queries.Raycast(Vec3d(0.0f, 50.0f, 0.0f), Vec3d(0.0f, -1.0f, 0.0f), 100.0f);
    EXPECT_TRUE(hit.Hit); // the cooked brush collision is solid and positioned
}

// A material that references a source PNG: the cook must produce a runtime .stex
// and the COOK=OFF registration must bind it under the source virtual path so the
// material's asset://...png reference resolves at load.
TEST_F(BrushCollisionCookTest, MaterialTextureCooksAndRegistersUnderSourcePath)
{
    const fs::path checkerMat = Root / "materials/dev/checker.smat";
    std::ofstream(checkerMat, std::ios::trunc)
        << R"({"version":1,"base_color_texture":"asset://textures/dev/checker.png"})";
    const fs::path checkerPng = Root / "textures/dev/checker.png";
    fs::create_directories(checkerPng.parent_path());
    {
        std::ofstream png(checkerPng, std::ios::binary | std::ios::trunc);
        png.write(reinterpret_cast<const char*>(kCheckerPng), sizeof(kCheckerPng));
    }

    EditorDocument doc(Logging);
    doc.SetDefaultMaterial(AssetRef{ AssetType::Material, "asset://materials/dev/checker.smat" });
    doc.GetScene().CreateBrush(Vec3d{ 0, 0, 0 });
    const fs::path levelPath = Root / "levels/textured.json";
    fs::create_directories(levelPath.parent_path());
    ASSERT_TRUE(doc.SaveAs(levelPath.generic_string()));

    const DocumentCookResult result = CookDocument(levelPath, Root, /*cellSize*/ 16.0);
    ASSERT_TRUE(result.Success) << result.Error;

    // The cook cooked the source PNG into a runtime .stex under .cooked/ (the
    // artifact keeps the source path and appends .stex; the virtual path stays
    // asset://...png).
    EXPECT_TRUE(fs::exists(Root / ".cooked/textures/dev/checker.png.stex"));

    // A COOK=OFF player mounts cooked content: the physical scan keys the meshes,
    // RegisterCookedAssets adds the texture under its source virtual path.
    LoggingProvider logging;
    AssetRegistry registry(logging);
    ScanAssetsDirectory((Root / ".cooked").generic_string(), registry);
    EXPECT_GE(RegisterCookedAssets(Root.generic_string(), registry), 1);

    const AssetRecord* texture = registry.FindByPath("asset://textures/dev/checker.png");
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->Type, AssetType::Texture);
    EXPECT_TRUE(fs::exists(texture->FilePath));
}
