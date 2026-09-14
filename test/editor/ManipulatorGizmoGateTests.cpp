#include "WorkspaceFixture.h"

#include "editmodes/ManipulatorSession.h"
#include "tools/ToolRegistry.h"
#include "viewport/EditorViewport.h"
#include "viewport/ViewportProjection.h"
#include "workspace/WorkspaceInteractionRuntime.h"

// A tool whose whole job is its own interaction declares that the transform
// gizmo does not belong on screen, and the session honours it at every entry
// point: no visuals, no hover, no press taken. Nothing but the session learns
// which tool it was.
namespace
{
class GizmoGateTest : public WorkspaceTest
{
protected:
    [[nodiscard]] ManipulatorSession& Session() { return *Workspace.Interaction.Manipulators; }
    [[nodiscard]] ToolContext& Context() { return *Workspace.Interaction.Context; }

    [[nodiscard]] static EditorViewport TopViewport()
    {
        EditorViewport viewport;
        viewport.ApplyOrientation(ViewportOrientation::Top);
        viewport.Id = ViewportId{ 1 };
        viewport.RegionMin = ImVec2(0.0f, 0.0f);
        viewport.RegionMax = ImVec2(400.0f, 400.0f);
        return viewport;
    }

    [[nodiscard]] std::size_t VisualLines(const EditorViewport& viewport)
    {
        ManipulatorVisual visual;
        Session().BuildVisuals(viewport, visual);
        return visual.Lines.size();
    }
};
}

TEST_F(GizmoGateTest, AToolThatOwnsItsInteractionHidesTheGizmo)
{
    const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    SelectEntity(brush);
    EditorViewport top = TopViewport();
    ASSERT_TRUE(Workspace.Interaction.Tools->Activate("select"));
    ASSERT_GT(VisualLines(top), 0u) << "the select tool shows the gizmo";

    // Hover a gizmo handle under Select: the bounds corner of the selected brush.
    const ImVec2 corner = ViewportProjection(top).WorldToPixel(Vec3d{ 1, 0, 1 })->Pixel;
    Session().UpdateHover(top, corner);

    ASSERT_TRUE(Workspace.Interaction.Tools->Activate("clip"));
    EXPECT_EQ(VisualLines(top), 0u) << "the gizmo is still drawn";
    EXPECT_EQ(Session().OnPointerDown(Context(), top, PointerEvent{ .Position = corner }), InputConsumed::No)
        << "the gizmo still takes the press";
    Session().UpdateHover(top, corner);
    EXPECT_EQ(VisualLines(top), 0u);

    ASSERT_TRUE(Workspace.Interaction.Tools->Activate("facecarve"));
    EXPECT_EQ(VisualLines(top), 0u);

    ASSERT_TRUE(Workspace.Interaction.Tools->Activate("select"));
    EXPECT_GT(VisualLines(top), 0u) << "the gizmo did not come back";
}
