// End-to-end: an authored brush becomes walkable collision with zero collision
// authoring. Cook a brush level -> a collision sidecar + per-cell .scol blobs ->
// LoadZoneCollision spawns static colliders -> PhysicsScene makes static bodies
// -> a downward ray hits the cooked brush. Headless: no AssetSystem, no graphics.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

#include <core/assets/AssetRef.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <physics/CollisionShapeCache.h>
#include <physics/PhysicsQueries.h>
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
    ecs.RegisterComponent<Collider>();
    ecs.RegisterComponent<RigidBody>();

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
