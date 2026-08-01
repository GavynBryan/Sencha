#include "WorkspaceFixture.h"

#include "document/tools/BrushTool.h"
#include "editmodes/ManipulatorSession.h"
#include "editmodes/TransformSpace.h"
#include "tools/ToolRegistry.h"

// The active tool and the gizmo state are the user's preferences, not the
// document's. Switching zones rebinds the editing stack to a different document,
// which must not quietly hand those preferences back to their defaults.
namespace
{

class WorkspaceFocusPreferenceTest : public WorkspaceTest
{
protected:
    [[nodiscard]] ToolRegistry& Tools() { return *Workspace.Interaction.Tools; }
    [[nodiscard]] ManipulatorSession& Manipulators() { return *Workspace.Interaction.Manipulators; }
};

TEST_F(WorkspaceFocusPreferenceTest, TheActiveToolSurvivesAFocusChange)
{
    const ZoneId second = AddSecondZone();
    ASSERT_TRUE(second.IsValid());
    ASSERT_TRUE(Tools().Activate("brush"));
    const int chosen = Tools().GetActiveIndex();

    ASSERT_TRUE(Workspace.World.SetFocusZone(second));

    EXPECT_EQ(Tools().GetActiveIndex(), chosen);
    ASSERT_NE(Tools().GetActiveTool(), nullptr);
    EXPECT_EQ(Tools().GetActiveTool()->GetId(), "brush");
}

TEST_F(WorkspaceFocusPreferenceTest, TheGizmoModeAndSpaceSurviveAFocusChange)
{
    const ZoneId second = AddSecondZone();
    ASSERT_TRUE(second.IsValid());
    Manipulators().SetTransformMode(TransformMode::Rotate);
    const TransformSpace space = Manipulators().GetTransformSpace();
    Manipulators().CycleTransformSpace();
    const TransformSpace cycled = Manipulators().GetTransformSpace();
    ASSERT_NE(cycled, space);

    ASSERT_TRUE(Workspace.World.SetFocusZone(second));

    EXPECT_EQ(Manipulators().GetTransformMode(), TransformMode::Rotate);
    EXPECT_EQ(Manipulators().GetTransformSpace(), cycled);
}

TEST_F(WorkspaceFocusPreferenceTest, TheEditingStackStillWorksAfterAFocusChange)
{
    const ZoneId second = AddSecondZone();
    ASSERT_TRUE(second.IsValid());
    ASSERT_TRUE(Workspace.World.SetFocusZone(second));

    // The rebound stack edits the newly focused document, not the outgoing one.
    const EntityId brush = AddBrush(Vec3d{ 0, 0, 0 });
    SelectElements(brush, MeshElementKind::Face, { 0 });
    Workspace.BeginInsetOnSelectedFaces(0.1f);

    EXPECT_TRUE(Workspace.HasPendingInset());
    Workspace.CancelPendingElementEdit();
    EXPECT_FALSE(Commands.HasPendingEdit());
}

TEST_F(WorkspaceFocusPreferenceTest, ToolsAreTheSameObjectsAcrossAFocusChange)
{
    const ZoneId second = AddSecondZone();
    ASSERT_TRUE(second.IsValid());
    const ITool* before = Tools().GetActiveTool();
    ManipulatorSession* session = &Manipulators();

    ASSERT_TRUE(Workspace.World.SetFocusZone(second));

    // Rebound, not rebuilt: anything a tool holds (its authoring settings) is
    // still there because the tool itself is still there.
    EXPECT_EQ(Tools().GetActiveTool(), before);
    EXPECT_EQ(&Manipulators(), session);
}

}
