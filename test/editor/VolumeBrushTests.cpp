// A volume brush is air: it never cooks, blocks a ray cast against surfaces,
// or appears in a floor plan, and the draw set keeps it out of the solid pass.

#include "WorkspaceFixture.h"

#include "document/BrushComponents.h"
#include "document/BrushCookInput.h"
#include "operations/FloorPlan.h"
#include "render/BrushDrawSet.h"
#include "viewport/Picking.h"

#include <world/registry/Registry.h>

#include <gtest/gtest.h>

namespace
{
class VolumeBrushTest : public WorkspaceTest
{
protected:
    VolumeBrushTest()
        : Bakes(BrushBakeCache::Gpu{
              .Bake = [this](const MeshGeometry&) { return StaticMeshHandle{ NextMesh++, 1 }; },
              .Destroy = [](StaticMeshHandle) {},
              .Lease = [this](const AssetRef&) { return MaterialHandle{ NextMaterial++, 1 }; },
              .Release = [](MaterialHandle) {},
          })
    {
    }

    EntityId Volume(Vec3d position, Vec3d half)
    {
        const EntityId entity = AddBrush(position, half);
        Scene().GetRegistry().Components.AddComponent(entity, BrushVolume{});
        return entity;
    }

    const AssetRef Default{ AssetType::Material, "asset://materials/dev/gray.smat" };
    std::uint32_t NextMesh = 1;
    std::uint32_t NextMaterial = 1;
    BrushDrawSet Draws;
    BrushBakeCache Bakes;
};
}

TEST_F(VolumeBrushTest, AVolumeNeverCooks)
{
    (void)AddBrush({ 0, -0.5, 0 }, { 4, 0.5, 4 });
    (void)Volume({ 0, 1.5, 0 }, { 4, 1.5, 4 });
    Scene().RefreshDerivedTransforms();
    EXPECT_EQ(CollectCookBrushes(Scene(), Default).size(), 1u);
}

TEST_F(VolumeBrushTest, ARayCastAgainstSurfacesPassesThroughAVolume)
{
    (void)AddBrush({ 0, -0.5, 0 }, { 4, 0.5, 4 });
    (void)Volume({ 0, 1.5, 0 }, { 4, 1.5, 4 });
    Scene().RefreshDerivedTransforms();
    const std::optional<SurfaceHit> hit =
        CastRayAgainstBrushes(Scene(), Ray3d(Vec3d{ 0, 10, 0 }, Vec3d{ 0, -1, 0 }), 100.0f, false);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Point.Y, 0.0f, 1e-4f) << "the ray reached the floor, not the volume's top";
}

TEST_F(VolumeBrushTest, AFloorPlanCutsOnlySolids)
{
    const EntityId wall = AddBrush({ 0, 1.5, 4.25 }, { 4, 1.5, 0.25 });
    (void)Volume({ 0, 1.5, 0 }, { 4, 1.5, 4 });
    Scene().RefreshDerivedTransforms();
    const FloorPlan plan = SectionLevel(Workspace.ActiveDocument(), 1.0f);
    ASSERT_EQ(plan.Brushes.size(), 1u);
    EXPECT_EQ(plan.Brushes.front().Entity, wall);
    EXPECT_TRUE(plan.Points.empty());
}

TEST_F(VolumeBrushTest, TheDrawSetFollowsTheVolumeTagBothWays)
{
    const EntityId room = AddBrush({ 0, 1.5, 0 }, { 4, 1.5, 4 });
    Scene().RefreshDerivedTransforms();
    ASSERT_TRUE(Draws.Refresh(Scene(), Bakes, Default));
    ASSERT_EQ(Draws.Find(room)->Meshes.front().Role, BrushRole::Solid);
    EXPECT_TRUE(Draws.Find(room)->Meshes.front().VolumeFill.empty());

    Scene().GetRegistry().Components.AddComponent(room, BrushVolume{});
    EXPECT_TRUE(Draws.Refresh(Scene(), Bakes, Default)) << "becoming a volume is a change";
    EXPECT_EQ(Draws.Find(room)->Meshes.front().Role, BrushRole::Volume);
    EXPECT_EQ(Draws.Find(room)->Meshes.front().VolumeFill.size(), 36u) << "a box is twelve triangles";
    EXPECT_FALSE(Draws.Refresh(Scene(), Bakes, Default));

    Scene().GetRegistry().Components.RemoveComponent<BrushVolume>(room);
    EXPECT_TRUE(Draws.Refresh(Scene(), Bakes, Default));
    EXPECT_EQ(Draws.Find(room)->Meshes.front().Role, BrushRole::Solid);
    EXPECT_TRUE(Draws.Find(room)->Meshes.front().VolumeFill.empty());
}
