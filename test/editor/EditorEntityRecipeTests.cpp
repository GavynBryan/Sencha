#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/EditorEntityRecipe.h"
#include "document/WorldDockAuthoring.h"
#include "document/WorldDocument.h"

#include <core/logging/LoggingProvider.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>

namespace
{

class EditorEntityRecipeTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        World.NewWorld("RecipeWorld");
        ZoneA = World.FocusZone();
        ZoneB = World.AddZone(World.Manifest().Graphs[0].Id, "Zone B");
        ASSERT_TRUE(World.SetZoneBounds(ZoneA,
            Aabb3d::FromMinMax(Vec3d{ -5, -5, 0 }, Vec3d{ 5, 5, 10 })));
        ASSERT_TRUE(World.SetZoneBounds(ZoneB,
            Aabb3d::FromMinMax(Vec3d{ -5, -5, -10 }, Vec3d{ 5, 5, 0 })));
    }

    EditorCreateContext Context()
    {
        return EditorCreateContext{
            .World = &World,
            .ActiveZone = ZoneA,
            .ActiveGraph = World.Manifest().Graphs[0].Id,
            .PlacementPoint = {},
            .PlacementNormal = Vec3d::Forward(),
        };
    }

    EntityId CreateDock()
    {
        return World.WorldSceneDocument().RestoreEntity(WorldDockRecipe{}.Build(Context()));
    }

    bool HasRule(std::string_view rule) const
    {
        for (const ContentRiskRecord& record : World.ValidationRecords())
            if (record.RuleId == rule)
                return true;
        return false;
    }

    LoggingProvider Logging;
    WorldDocument World{ Logging };
    ZoneId ZoneA;
    ZoneId ZoneB;
};

} // namespace

TEST_F(EditorEntityRecipeTest, DockRecipeBuildsCompleteContextualSnapshot)
{
    const EntitySnapshot snapshot = WorldDockRecipe{}.Build(Context());
    const EntityId entity = World.WorldSceneDocument().RestoreEntity(snapshot);
    const Registry& registry = World.WorldSceneDocument().GetRegistry();
    const WorldDock* dock = registry.Components.TryGet<WorldDock>(entity);
    const LocalTransform* transform = registry.Components.TryGet<LocalTransform>(entity);

    ASSERT_NE(dock, nullptr);
    ASSERT_NE(transform, nullptr);
    EXPECT_TRUE(dock->Id.IsValid());
    EXPECT_EQ(dock->ZoneA, ZoneA);
    EXPECT_EQ(dock->ZoneB, ZoneB);
    EXPECT_EQ(transform->Value.Position, Vec3d::Zero());
    const Vec3d dockNormal = -transform->Value.Forward();
    EXPECT_NEAR(dockNormal.X, Vec3d::Forward().X, 1e-5f);
    EXPECT_NEAR(dockNormal.Y, Vec3d::Forward().Y, 1e-5f);
    EXPECT_NEAR(dockNormal.Z, Vec3d::Forward().Z, 1e-5f);
}

TEST_F(EditorEntityRecipeTest, ExplicitCreatesMintIdsWhileSnapshotRestorePreservesStoredId)
{
    const EntitySnapshot first = WorldDockRecipe{}.Build(Context());
    const EntitySnapshot second = WorldDockRecipe{}.Build(Context());
    EditorDocument& document = World.WorldSceneDocument();
    const EntityId firstEntity = document.RestoreEntity(first);
    const EntityId restoredEntity = document.RestoreEntity(first);
    const EntityId secondEntity = document.RestoreEntity(second);
    const Registry& registry = document.GetRegistry();

    const DockId firstId = registry.Components.TryGet<WorldDock>(firstEntity)->Id;
    EXPECT_EQ(registry.Components.TryGet<WorldDock>(restoredEntity)->Id, firstId);
    EXPECT_NE(registry.Components.TryGet<WorldDock>(secondEntity)->Id, firstId);
}

TEST_F(EditorEntityRecipeTest, DuplicateBatchMintsIdentityOnceAndRemapsGateBinding)
{
    EditorDocument& document = World.WorldSceneDocument();
    EditorScene& scene = document.GetScene();
    const EntityId dockEntity = document.RestoreEntity(WorldDockRecipe{}.Build(Context()));
    const WorldDock* original = document.GetRegistry().Components.TryGet<WorldDock>(dockEntity);
    ASSERT_NE(original, nullptr);
    const DockId originalId = original->Id;

    const EntityId gateEntity = scene.CreateEntity({});
    scene.GetRegistry().Components.AddComponent(gateEntity, DockGateBinding{ originalId });
    std::vector<EntitySnapshot> snapshots{
        document.CaptureEntity(dockEntity), document.CaptureEntity(gateEntity),
    };

    RemapWorldConnectionDuplicateSnapshots(World, snapshots);
    const EntityId copiedDock = document.RestoreEntity(snapshots[0]);
    const EntityId copiedGate = document.RestoreEntity(snapshots[1]);
    const WorldDock* dock = document.GetRegistry().Components.TryGet<WorldDock>(copiedDock);
    const DockGateBinding* gate =
        document.GetRegistry().Components.TryGet<DockGateBinding>(copiedGate);
    ASSERT_NE(dock, nullptr);
    ASSERT_NE(gate, nullptr);
    EXPECT_NE(dock->Id, originalId);
    EXPECT_EQ(gate->Id, dock->Id);

    const DockId duplicatedId = dock->Id;
    scene.DestroyEntity(copiedDock);
    scene.DestroyEntity(copiedGate);
    const EntityId redoDock = document.RestoreEntity(snapshots[0]);
    const EntityId redoGate = document.RestoreEntity(snapshots[1]);
    EXPECT_EQ(document.GetRegistry().Components.TryGet<WorldDock>(redoDock)->Id,
              duplicatedId);
    EXPECT_EQ(document.GetRegistry().Components.TryGet<DockGateBinding>(redoGate)->Id,
              duplicatedId);
}

TEST_F(EditorEntityRecipeTest, LinkRecipeHasNoFakeSpatialTransform)
{
    const EntitySnapshot snapshot = WorldLinkRecipe(ZoneB).Build(Context());
    const EntityId entity = World.WorldSceneDocument().RestoreEntity(snapshot);
    const Registry& registry = World.WorldSceneDocument().GetRegistry();
    const WorldLink* link = registry.Components.TryGet<WorldLink>(entity);

    ASSERT_NE(link, nullptr);
    EXPECT_TRUE(link->Id.IsValid());
    EXPECT_EQ(link->ZoneA, ZoneA);
    EXPECT_EQ(link->ZoneB, ZoneB);
    EXPECT_EQ(registry.Components.TryGet<LocalTransform>(entity), nullptr);
}

TEST_F(EditorEntityRecipeTest, InvalidContextReturnsEmptySnapshot)
{
    EditorCreateContext invalid;
    EXPECT_FALSE(WorldDockRecipe{}.Build(invalid).Components.IsObject());
    EXPECT_FALSE(WorldLinkRecipe(ZoneB).Build(invalid).Components.IsObject());
}

TEST_F(EditorEntityRecipeTest, DockValidationNamesEndpointPlaneAndArmFailures)
{
    const EntityId entity = CreateDock();
    Registry& registry = World.WorldSceneDocument().GetScene().GetRegistry();
    WorldDock* dock = registry.Components.TryGet<WorldDock>(entity);
    ASSERT_NE(dock, nullptr);

    dock->ZoneB = ZoneId{ 0xdead };
    dock->HalfExtents.X = 0.0f;
    dock->SideAArmBounds.Max.Z = 1.0f;
    World.Revalidate();
    EXPECT_TRUE(HasRule("partition.dock.zone_unresolved"));
    EXPECT_TRUE(HasRule("partition.dock.plane_degenerate"));
    EXPECT_TRUE(HasRule("partition.dock.arm_bounds_wrong_side"));

    dock->ZoneB = dock->ZoneA;
    World.Revalidate();
    EXPECT_TRUE(HasRule("partition.dock.same_zone"));
}

TEST_F(EditorEntityRecipeTest, DuplicateDockIdsAreRejectedButParallelEdgesAreLegal)
{
    const EntityId first = CreateDock();
    const EntityId second = CreateDock();
    Registry& registry = World.WorldSceneDocument().GetScene().GetRegistry();
    WorldDock* a = registry.Components.TryGet<WorldDock>(first);
    WorldDock* b = registry.Components.TryGet<WorldDock>(second);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    World.Revalidate();
    EXPECT_FALSE(HasRule("partition.dock.id_duplicate"));

    b->Id = a->Id;
    World.Revalidate();
    EXPECT_TRUE(HasRule("partition.dock.id_duplicate"));
}

TEST(WorldDockAuthoring, SwapSidesPreservesWorldArmPlacementAndReversesSemantics)
{
    WorldDock dock;
    dock.ZoneA = ZoneId{ 1 };
    dock.ZoneB = ZoneId{ 2 };
    dock.Directions = DockDirectionAToB;
    dock.SideAArmBounds = Aabb3d::FromMinMax({ -1, -2, -4 }, { 2, 3, 0 });
    dock.SideBArmBounds = Aabb3d::FromMinMax({ -3, -1, 0 }, { 4, 5, 6 });
    Transform3f transform;
    transform.Position = { 10, 4, -2 };
    transform.Rotation = Quatf::FromAxisAngle(Vec3d::Up(), 0.37f);

    const Vec3d oldBMinimumWorld = transform.TransformPoint(dock.SideBArmBounds.Min);
    SwapWorldDockSides(dock, transform);

    EXPECT_EQ(dock.ZoneA, ZoneId{ 2 });
    EXPECT_EQ(dock.ZoneB, ZoneId{ 1 });
    EXPECT_EQ(dock.Directions, DockDirectionBToA);
    EXPECT_LE(dock.SideAArmBounds.Max.Z, 0.0f);
    EXPECT_GE(dock.SideBArmBounds.Min.Z, 0.0f);
    const Vec3d newCornerWorld = transform.TransformPoint({
        dock.SideAArmBounds.Max.X,
        dock.SideAArmBounds.Min.Y,
        dock.SideAArmBounds.Max.Z,
    });
    EXPECT_NEAR(newCornerWorld.X, oldBMinimumWorld.X, 1.0e-4f);
    EXPECT_NEAR(newCornerWorld.Y, oldBMinimumWorld.Y, 1.0e-4f);
    EXPECT_NEAR(newCornerWorld.Z, oldBMinimumWorld.Z, 1.0e-4f);
}
