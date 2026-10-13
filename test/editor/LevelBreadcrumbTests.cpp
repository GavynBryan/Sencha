#include "WorkspaceFixture.h"

#include "workspace/LevelBreadcrumb.h"
#include "workspace/LevelLocation.h"

#include <gtest/gtest.h>

// Where the work is: a world's breadcrumb is the world, the graph and zone
// being worked in, then what is selected, each keyed to reveal itself.
TEST_F(WorkspaceTest, ABreadcrumbRunsFromTheWorldToWhatIsSelected)
{
    ASSERT_TRUE(Workspace.World.IsWorld());
    const ZoneHeader& zone = Workspace.World.Manifest().Zones.front();
    ASSERT_TRUE(Workspace.World.SetFocusZone(zone.Id));
    const EntityId brush = AddBrush({ 0, 0, 0 });
    Select({ SelectableRef::EntitySelection(Registry(), brush) });

    const WorkspaceLocationFacet crumb = LevelBreadcrumb(Workspace, "ignored for a world");
    ASSERT_GE(crumb.Parts.size(), 2u);
    EXPECT_EQ(crumb.Parts.front().Key, LevelLocationKey(WorldScenePlace{}));
    EXPECT_FALSE(crumb.MapAction.empty());
    EXPECT_TRUE(crumb.Parts.back().Selection);
    EXPECT_EQ(crumb.Parts.back().Key, LevelLocationKey(Scene().PersistentIdOf(brush)));
}
