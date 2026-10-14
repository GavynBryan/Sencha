// A space's shell, as the scene evaluates it: its walls are solid pieces of the
// space's own evaluation, cooked and cast against like any brush, rebuilt when
// what they were built from changes, and mapped back to the volume's faces.

#include "WorkspaceFixture.h"

#include "document/BrushComponents.h"
#include "document/BrushCookInput.h"
#include "document/SpaceComponents.h"
#include "render/BrushDrawSet.h"
#include "viewport/Picking.h"

#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

namespace
{
class SpaceShellTest : public WorkspaceTest
{
protected:
    EntityId ShelledSpace(Vec3d center, Vec3d half, float thickness = 0.25f)
    {
        const EntityId entity = AddBrush(center, half);
        World& world = Scene().GetRegistry().Components;
        world.AddComponent(entity, BrushVolume{});
        world.AddComponent(entity, Space{});
        world.AddComponent(entity, Shell{ .Thickness = thickness });
        Scene().RefreshDerivedTransforms();
        return entity;
    }

    Aabb3d CookedBounds()
    {
        Aabb3d bounds = Aabb3d::Empty();
        for (const CookBrushGeometry& brush : CollectCookBrushes(Scene(), Default))
            bounds.ExpandToInclude(brush.WorldBounds);
        return bounds;
    }

    const AssetRef Default{ AssetType::Material, "asset://materials/dev/gray.smat" };
};

void ExpectNear(const Vec3d& actual, const Vec3d& expected)
{
    EXPECT_NEAR(actual.X, expected.X, 1e-3f);
    EXPECT_NEAR(actual.Y, expected.Y, 1e-3f);
    EXPECT_NEAR(actual.Z, expected.Z, 1e-3f);
}
}

TEST_F(SpaceShellTest, AShelledSpaceCooksItsWallsAndNotItsAir)
{
    (void)ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    EXPECT_EQ(CollectCookBrushes(Scene(), Default).size(), 6u);
    const Aabb3d bounds = CookedBounds();
    ExpectNear(bounds.Min, { -4.25f, -0.25f, -3.25f });
    ExpectNear(bounds.Max, { 4.25f, 3.25f, 3.25f });
}

TEST_F(SpaceShellTest, ASpaceWithoutAShellCooksNothing)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    Scene().GetRegistry().Components.RemoveComponent<Shell>(room);
    EXPECT_TRUE(CollectCookBrushes(Scene(), Default).empty());
}

TEST_F(SpaceShellTest, AFacePaintedOpenGetsNoWall)
{
    const AssetRef open{ AssetType::Material, "asset://materials/dev/open.smat" };
    Scene().SetOpenFaceMaterial(open);
    const EntityId courtyard = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    BrushMesh mesh = *Scene().TryGetBrushMesh(courtyard);
    for (BrushFace& face : mesh.Faces)
        if (face.Normal.Y > 0.99f)
            face.Material.Material = open;
    Scene().SetBrushMesh(courtyard, mesh);
    EXPECT_EQ(CollectCookBrushes(Scene(), Default).size(), 5u);
    EXPECT_NEAR(CookedBounds().Max.Y, 3.0f, 1e-3f) << "the walls stop on the open top";
}

TEST_F(SpaceShellTest, ARayFromInsideTheRoomLandsOnItsGeneratedFloor)
{
    (void)ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    const std::optional<SurfaceHit> hit =
        CastRayAgainstBrushes(Scene(), Ray3d(Vec3d{ 0, 1.5f, 0 }, Vec3d{ 0, -1, 0 }), 100.0f, false);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Point.Y, 0.0f, 1e-4f);
    EXPECT_GT(hit->Normal.Y, 0.99f);
}

TEST_F(SpaceShellTest, MovingOrThickeningTheSpaceRebuildsItsWalls)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    (void)CookedBounds();

    Scene().GetRegistry().Components.TryGet<LocalTransform>(room)->Value.Position.X = 10.0f;
    Scene().RefreshDerivedTransforms();
    ExpectNear(CookedBounds().Min, { 5.75f, -0.25f, -3.25f });

    Scene().GetRegistry().Components.TryGet<Shell>(room)->Thickness = 0.5f;
    ExpectNear(CookedBounds().Min, { 5.5f, -0.5f, -3.5f });
}

TEST_F(SpaceShellTest, WallsAreAsThickInTheWorldWhateverTheSpacesScale)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 2, 1.5f, 3 });
    Scene().GetRegistry().Components.TryGet<LocalTransform>(room)->Value.Scale = Vec3d{ 2, 1, 1 };
    Scene().RefreshDerivedTransforms();
    const Aabb3d bounds = CookedBounds();
    ExpectNear(bounds.Min, { -4.25f, -0.25f, -3.25f });
    ExpectNear(bounds.Max, { 4.25f, 3.25f, 3.25f });
}

TEST_F(SpaceShellTest, AWallsInnerFaceIsTheVolumeFaceItStandsOn)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    const BrushEvaluated* evaluated = Scene().TryGetBrushPieces(room);
    ASSERT_NE(evaluated, nullptr);
    int walls = 0;
    for (const BrushPiece& piece : evaluated->Pieces)
    {
        const BrushEvaluatedMesh& mesh = evaluated->Meshes[piece.MeshIndex];
        if (mesh.Role == BrushRole::Volume)
            continue;
        ++walls;
        int mapped = 0;
        for (std::uint32_t face = 0; face < mesh.Mesh->Faces.size(); ++face)
            if (const std::optional<std::uint32_t> source = SourceFaceFor(*evaluated, piece, face))
            {
                ++mapped;
                EXPECT_LT(mesh.Mesh->Faces[face].Normal.Dot(Scene().TryGetBrushMesh(room)->Faces[*source].Normal),
                          -0.99f);
            }
        EXPECT_EQ(mapped, 1) << "only the inner face stands for a volume face";
    }
    EXPECT_EQ(walls, 6);
}

TEST_F(SpaceShellTest, ARefusedShellSaysWhyAndLeavesTheSpace)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 }, 0.25f);
    BrushMesh bent = *Scene().TryGetBrushMesh(room);
    bent.Vertices.front().Position.Y += 0.4f;
    Scene().SetBrushMesh(room, bent);
    EXPECT_TRUE(CollectCookBrushes(Scene(), Default).empty());
    ASSERT_FALSE(Scene().ShellProblems(room).empty());
    EXPECT_EQ(Scene().ShellProblems(room).front().Kind, ShellProblemKind::NonPlanarFace);
    EXPECT_TRUE(Scene().GetRegistry().Components.HasComponent<Space>(room));
}

TEST_F(SpaceShellTest, TheDrawSetHoldsTheAirAndTheWallsApart)
{
    const EntityId room = ShelledSpace({ 0, 1.5f, 0 }, { 4, 1.5f, 3 });
    BrushBakeCache bakes(BrushBakeCache::Gpu{
        .Bake = [next = 1u](const MeshGeometry&) mutable { return StaticMeshHandle{ next++, 1 }; },
        .Destroy = [](StaticMeshHandle) {},
        .Lease = [](const AssetRef&) { return MaterialHandle{ 1, 1 }; },
        .Release = [](MaterialHandle) {},
    });
    BrushDrawSet draws;
    ASSERT_TRUE(draws.Refresh(Scene(), bakes, Default));
    int volumes = 0;
    int solids = 0;
    for (const BrushDrawMesh& mesh : draws.Find(room)->Meshes)
        (mesh.Role == BrushRole::Volume ? volumes : solids) += 1;
    EXPECT_EQ(volumes, 1);
    EXPECT_EQ(solids, 6);
}
