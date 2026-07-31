#include "WorkspaceFixture.h"

#include "document/EditorEntityRecipe.h"

#include <zone/WorldConnectionComponents.h>

// A duplicated world dock must get its own minted identity, whichever route
// produced the copy. The routes are the menu duplicate, the gizmo Shift-drag,
// and the repeat of a recorded drag: all three share one remap, so none of them
// can leave a copy wearing the original's id.
namespace
{

class WorkspaceDuplicateRemapTest : public WorkspaceTest
{
protected:
    // Docks live on the world scene, so edit that document rather than a zone.
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Workspace.World.FocusWorldScene();
    }

    [[nodiscard]] EntityId CreateDock()
    {
        const EditorCreateContext context{
            .World = &Workspace.World,
            .ActiveZone = Workspace.World.Manifest().Zones[0].Id,
            .ActiveGraph = Workspace.World.Manifest().Graphs[0].Id,
            .PlacementPoint = {},
            .PlacementNormal = Vec3d::Forward(),
        };
        return Workspace.ActiveDocument().RestoreEntity(WorldDockRecipe{}.Build(context));
    }

    [[nodiscard]] DockId DockIdOf(EntityId entity)
    {
        const WorldDock* dock =
            Workspace.ActiveDocument().GetRegistry().Components.TryGet<WorldDock>(entity);
        return dock == nullptr ? DockId{} : dock->Id;
    }

    // The dock entity that is not `original`, i.e. the copy.
    [[nodiscard]] EntityId CopyOf(EntityId original)
    {
        for (const EntityId entity : Scene().GetAllEntities())
        {
            if (entity == original)
                continue;
            if (Workspace.ActiveDocument().GetRegistry().Components.TryGet<WorldDock>(entity) != nullptr)
                return entity;
        }
        return {};
    }
};

TEST_F(WorkspaceDuplicateRemapTest, DuplicatingADockMintsANewId)
{
    const EntityId dock = CreateDock();
    const DockId original = DockIdOf(dock);
    ASSERT_TRUE(original.IsValid());
    SelectEntity(dock);

    Workspace.Actions.Duplicate(false);

    const EntityId copy = CopyOf(dock);
    ASSERT_TRUE(copy.IsValid());
    EXPECT_TRUE(DockIdOf(copy).IsValid());
    EXPECT_NE(DockIdOf(copy), original);
}

TEST_F(WorkspaceDuplicateRemapTest, RepeatingADuplicateAlsoMintsANewId)
{
    const EntityId dock = CreateDock();
    const DockId original = DockIdOf(dock);
    ASSERT_TRUE(original.IsValid());
    SelectEntity(dock);

    // The offset route a recorded repeat replays. It shares the remap with the
    // plain duplicate above, so its copy is just as distinct.
    Workspace.Actions.DuplicateWithOffset(Vec3d{ 4, 0, 0 });

    const EntityId copy = CopyOf(dock);
    ASSERT_TRUE(copy.IsValid());
    EXPECT_TRUE(DockIdOf(copy).IsValid());
    EXPECT_NE(DockIdOf(copy), original);
}

}
