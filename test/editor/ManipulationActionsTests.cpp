#include "WorkspaceFixture.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "workspace/ManipulationActions.h"
#include "editmodes/ManipulatorSession.h"

#include <gtest/gtest.h>

// The Level's manipulation settings are actions that say whether they are in
// effect, so anything listing actions shows the current setting and changes it
// -- and the facts a readout shows follow.
namespace
{
class ManipulationActionsTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        OfferManipulationActions(Catalog, Context, Workspace);
        Layer = Stack.Push(Context, ActionRank::Workspace);
    }

    [[nodiscard]] bool Checked(std::string_view id) const { return Stack.Query(id).Checked; }

    ActionCatalog Catalog;
    ActionContext Context;
    ActionContextStack Stack;
    ActionContextStack::Layer Layer;
};
} // namespace

TEST_F(ManipulationActionsTest, OneOfEachSettingIsCheckedAndChoosingMovesTheCheck)
{
    EXPECT_TRUE(Checked("mode.object"));
    EXPECT_FALSE(Checked("mode.face"));
    ASSERT_TRUE(Stack.Invoke("mode.face"));
    EXPECT_TRUE(Checked("mode.face"));
    EXPECT_FALSE(Checked("mode.object"));

    ASSERT_TRUE(Stack.Invoke("gizmo.space.local"));
    EXPECT_TRUE(Checked("gizmo.space.local"));
    EXPECT_FALSE(Checked("gizmo.space.world"));

    ASSERT_TRUE(Stack.Invoke("grid.size.8"));
    EXPECT_TRUE(Checked("grid.size.8"));
    EXPECT_FALSE(Checked("grid.size.1"));
}

TEST_F(ManipulationActionsTest, TheReadoutFollowsTheSettings)
{
    const bool snapped = Workspace.Grid.SnapEnabled;
    ASSERT_TRUE(Stack.Invoke("snap.toggle"));
    EXPECT_EQ(Checked("snap.toggle"), !snapped);
    ASSERT_TRUE(Stack.Invoke("grid.size.16"));
    ASSERT_TRUE(Stack.Invoke("mode.vertex"));

    const WorkspaceManipulationFacet facet = ManipulationOf(Workspace);
    EXPECT_EQ(facet.Element, "Vertex");
    EXPECT_EQ(facet.Grid, "16");
    EXPECT_EQ(facet.Snap, !snapped);
    EXPECT_FALSE(facet.Mode.empty());
}

TEST_F(ManipulationActionsTest, WhatNeedsASelectionWaitsForOne)
{
    EXPECT_FALSE(Stack.Query("pivot.edit").Enabled);
    EXPECT_FALSE(Stack.Query("origin.center").Enabled);
    SelectEntity(AddBrush(Vec3d{}));
    EXPECT_TRUE(Stack.Query("pivot.edit").Enabled);
    EXPECT_TRUE(Stack.Query("origin.center").Enabled);
}
