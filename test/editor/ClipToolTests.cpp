#include "WorkspaceFixture.h"

#include "brush/BrushValidation.h"
#include "brush/BrushFaceFrame.h"
#include "brush/CarvePolygon.h"
#include "brush/CarveShape.h"
#include "document/tools/ClipTool.h"
#include "meshedit/ManipulationSink.h"
#include "tools/ToolRegistry.h"
#include "viewport/EditorViewport.h"
#include "viewport/ViewportProjection.h"
#include "workspace/BrushManipulationSink.h"
#include "workspace/WorkspaceInteractionRuntime.h"

#include <SDL3/SDL_keycode.h>

#include <cmath>
#include <numbers>

namespace
{
constexpr float kTol = 1e-4f;

class ClipToolTest : public WorkspaceTest
{
protected:
    [[nodiscard]] ToolContext& Context() { return *Workspace.Interaction.Context; }
    [[nodiscard]] ManipulationSink& Sink() { return *Workspace.Interaction.Sink; }
    [[nodiscard]] ClipTool& Tool()
    {
        for (const std::unique_ptr<ITool>& tool : Workspace.Interaction.Tools->GetTools())
            if (tool != nullptr && tool->GetId() == "clip")
                return static_cast<ClipTool&>(*tool);
        ADD_FAILURE() << "clip tool not registered";
        std::abort();
    }

    [[nodiscard]] Aabb3d BoundsOf(EntityId entity)
    {
        const BrushMesh* mesh = Scene().TryGetBrushMesh(entity);
        EXPECT_NE(mesh, nullptr);
        return mesh != nullptr ? BrushComputeBounds(*mesh) : Aabb3d{};
    }

    [[nodiscard]] static EditorViewport TopViewport()
    {
        EditorViewport viewport;
        viewport.ApplyOrientation(ViewportOrientation::Top);
        viewport.Id = ViewportId{ 1 };
        viewport.RegionMin = ImVec2(0.0f, 0.0f);
        viewport.RegionMax = ImVec2(400.0f, 400.0f);
        return viewport;
    }

    [[nodiscard]] static EditorViewport PerspectiveViewport()
    {
        EditorViewport viewport;
        viewport.ApplyOrientation(ViewportOrientation::Perspective);
        viewport.Id = ViewportId{ 2 };
        viewport.Camera.Position = Vec3d(0.0f, 0.0f, 6.0f); // looking down -Z at the origin
        viewport.RegionMin = ImVec2(0.0f, 0.0f);
        viewport.RegionMax = ImVec2(400.0f, 400.0f);
        return viewport;
    }

    [[nodiscard]] static ImVec2 PixelOf(const EditorViewport& viewport, Vec3d world)
    {
        const std::optional<ProjectedPoint> p = ViewportProjection(viewport).WorldToPixel(world);
        EXPECT_TRUE(p.has_value());
        return p.has_value() ? p->Pixel : ImVec2{};
    }

    // Draw the line from `a` to `b` (world points on the view's plane) and
    // release, leaving the tool pending.
    void Draw(EditorViewport& viewport, Vec3d a, Vec3d b)
    {
        std::unique_ptr<IInteraction> drag =
            Tool().BeginDrag(Context(), viewport, PointerEvent{ .Position = PixelOf(viewport, a) });
        ASSERT_NE(drag, nullptr);
        drag->OnPointerMove(Context(), viewport, PointerEvent{ .Position = PixelOf(viewport, b) });
        drag->OnPointerUp(Context(), viewport, PointerEvent{ .Position = PixelOf(viewport, b) });
        ASSERT_EQ(Tool().GetPhase(), ClipPhase::Pending);
    }

    void Press(SDL_Keycode key) { Tool().OnKeyDown(Context(), KeyDownEvent{ key, {} }); }
};
}

TEST_F(ClipToolTest, ASplitCommitsAsOneStepAndUndoesAsOne)
{
    const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    const BrushMesh original = *Scene().TryGetBrushMesh(brush);
    const Plane plane = Plane::FromNormalAndPoint(Vec3d{ 1, 0, 0 }, Vec3d{});
    std::vector<SplitEdit> edits;
    edits.push_back(SplitEdit{ brush, original, BrushOps::Clip(original, plane, true),
                               BrushOps::Clip(original, plane, false) });
    Sink().CommitSplits(std::move(edits));

    ASSERT_EQ(BrushCount(), 2u);
    EXPECT_NEAR(BoundsOf(brush).Min.X, 0.0f, kTol);
    EXPECT_NEAR(BoundsOf(brush).Max.X, 1.0f, kTol);
    EntityId other = {};
    for (const EntityId entity : Scene().GetAllEntities())
        if (entity != brush && Scene().TryGetBrushMesh(entity) != nullptr)
            other = entity;
    ASSERT_TRUE(other.IsValid());
    EXPECT_NEAR(BoundsOf(other).Min.X, -1.0f, kTol);
    EXPECT_NEAR(BoundsOf(other).Max.X, 0.0f, kTol);
    EXPECT_TRUE(Scene().TryGetWorldTransform(other)->NearlyEquals(*Scene().TryGetWorldTransform(brush), kTol));
    for (const EntityId entity : { brush, other })
    {
        BrushMesh copy = *Scene().TryGetBrushMesh(entity);
        EXPECT_TRUE(BrushValidateAndRepair(copy).Closed);
    }
    // Both halves are what is selected.
    EXPECT_EQ(Workspace.Selection.GetSelection().size(), 2u);

    Commands.Undo();
    EXPECT_EQ(BrushCount(), 1u);
    EXPECT_EQ(Scene().TryGetBrushMesh(brush)->Vertices.size(), original.Vertices.size());
    Commands.Redo();
    EXPECT_EQ(BrushCount(), 2u);
}

TEST_F(ClipToolTest, ASplitCopiesTheBrushAloneUnderItsParent)
{
    const EntityId parent = AddBrush({ 5, 0, 0 });
    const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    const EntityId child = AddBrush({ 0, 3, 0 });
    ASSERT_TRUE(Scene().SetParent(brush, parent));
    ASSERT_TRUE(Scene().SetParent(child, brush));
    Transform3f placed = *Scene().TryGetWorldTransform(brush);
    placed.Position = Vec3d{ 2, 1, 0 };
    placed.Rotation = Quatf::FromAxisAngle(Vec3d{ 0, 1, 0 }, std::numbers::pi_v<float> * 0.25f);
    placed.Scale = Vec3d{ 2.0f, 1.0f, 0.5f };
    Scene().SetWorldTransform(brush, placed);
    const Transform3f world = *Scene().TryGetWorldTransform(brush);

    const BrushMesh original = *Scene().TryGetBrushMesh(brush);
    const Plane local = Plane::FromNormalAndPoint(Vec3d{ 0, 0, 1 }, Vec3d{});
    std::vector<SplitEdit> edits;
    edits.push_back(SplitEdit{ brush, original, BrushOps::Clip(original, local, true),
                               BrushOps::Clip(original, local, false) });
    const std::size_t before = BrushCount();
    Sink().CommitSplits(std::move(edits));
    EXPECT_EQ(BrushCount(), before + 1) << "the child was not copied along";
    EntityId other = {};
    for (const EntityId entity : Scene().GetAllEntities())
        if (entity != brush && entity != parent && entity != child && Scene().TryGetBrushMesh(entity) != nullptr)
            other = entity;
    ASSERT_TRUE(other.IsValid());
    EXPECT_EQ(Scene().GetParent(other), parent);
    EXPECT_TRUE(Scene().TryGetWorldTransform(other)->NearlyEquals(world, 1e-3f));
    EXPECT_EQ(Scene().GetParent(child), brush);
}

TEST_F(ClipToolTest, ATopViewLineKeepsSplitsOrDropsHalves)
{
    for (const ClipMode mode : { ClipMode::KeepFront, ClipMode::KeepBack, ClipMode::Split })
    {
        Workspace.World.NewWorld("clip");
        const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
        SelectEntity(brush);
        EditorViewport top = TopViewport();
        Tool().SetMode(Context(), mode);
        // A line along Z at x = 0, drawn toward +Z: seen from above the front
        // side is one of the two halves; which one is the test's own fact.
        Draw(top, Vec3d{ 0, 0, -3 }, Vec3d{ 0, 0, 3 });
        ASSERT_TRUE(Tool().CanCommit());
        const Plane plane = *Tool().GetClipPlane();
        const bool frontIsPositiveX = plane.Normal.X > 0.0f;
        Press(SDLK_RETURN);
        EXPECT_EQ(Tool().GetPhase(), ClipPhase::Idle);

        if (mode == ClipMode::Split)
        {
            EXPECT_EQ(BrushCount(), 2u);
            continue;
        }
        EXPECT_EQ(BrushCount(), 1u);
        const Aabb3d bounds = BoundsOf(brush);
        const bool keptPositive = (mode == ClipMode::KeepFront) == frontIsPositiveX;
        EXPECT_NEAR(bounds.Min.X, keptPositive ? 0.0f : -1.0f, kTol);
        EXPECT_NEAR(bounds.Max.X, keptPositive ? 1.0f : 0.0f, kTol);
    }
}

TEST_F(ClipToolTest, EscapeRestoresAndAMissedLineCommitsNothing)
{
    const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    const BrushMesh original = *Scene().TryGetBrushMesh(brush);
    SelectEntity(brush);
    EditorViewport top = TopViewport();
    Tool().SetMode(Context(), ClipMode::KeepFront);
    Draw(top, Vec3d{ 0, 0, -3 }, Vec3d{ 0, 0, 3 });
    EXPECT_NEAR(BoundsOf(brush).Max.X - BoundsOf(brush).Min.X, 1.0f, kTol) << "no preview";
    Press(SDLK_ESCAPE);
    EXPECT_EQ(Tool().GetPhase(), ClipPhase::Idle);
    EXPECT_NEAR(BoundsOf(brush).Max.X - BoundsOf(brush).Min.X, 2.0f, kTol);
    EXPECT_FALSE(Commands.CanUndo());

    // A line beside the brush crosses nothing.
    Draw(top, Vec3d{ 4, 0, -3 }, Vec3d{ 4, 0, 3 });
    EXPECT_FALSE(Tool().CanCommit());
    Press(SDLK_RETURN);
    EXPECT_EQ(Tool().GetPhase(), ClipPhase::Pending) << "nothing to apply";
    Press(SDLK_ESCAPE);
    EXPECT_EQ(BrushCount(), 1u);
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(ClipToolTest, APerspectiveCutStaysWhereTheCameraLeftIt)
{
    const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    SelectEntity(brush);
    EditorViewport view = PerspectiveViewport();
    Context().Grid.SnapEnabled = false; // the points are the test's own, not the lattice's
    Tool().SetMode(Context(), ClipMode::Split);
    // A diagonal across the brush's near face (z = 1), which the press lands
    // on; the plane stands through it and the eye.
    Draw(view, Vec3d{ -0.6f, 0.9f, 1.0f }, Vec3d{ 0.6f, -0.9f, 1.0f });
    ASSERT_TRUE(Tool().CanCommit());
    const Plane before = *Tool().GetClipPlane();
    const BrushMesh previewBefore = *Scene().TryGetBrushMesh(brush);

    view.Camera.Position = Vec3d(4.0f, 3.0f, -2.0f);
    view.Camera.Yaw = 0.7f;
    Press(SDLK_TAB); // a mode change rebuilds the preview: from the stored plane
    Press(SDLK_TAB);
    Press(SDLK_TAB);
    ASSERT_EQ(Tool().GetMode(), ClipMode::Split);
    const Plane after = *Tool().GetClipPlane();
    EXPECT_NEAR(before.Normal.Dot(after.Normal), 1.0f, kTol);
    EXPECT_NEAR(before.D, after.D, kTol);
    EXPECT_EQ(Scene().TryGetBrushMesh(brush)->Vertices.size(), previewBefore.Vertices.size());
    Press(SDLK_ESCAPE);
}

TEST_F(ClipToolTest, ADragKeepsTheSnapPlaneItStartedOn)
{
    // The press lands on the near face of a tall wall; the drag then crosses a
    // second brush in front of it on screen. The endpoints stay on the wall's
    // face plane, so the plane is the one the user drew, not one that jumped.
    const EntityId wall = AddBrush({ 0, 0, -2 }, { 3, 3, 0.5f });
    const EntityId blocker = AddBrush({ 1, 0, 1 }, { 0.5f, 0.5f, 0.5f });
    SelectEntity(wall);
    EditorViewport view = PerspectiveViewport();
    Context().Grid.SnapEnabled = false;
    Tool().SetMode(Context(), ClipMode::Split);
    const Vec3d a{ -1.5f, 1.5f, -1.5f }; // on the wall's near face z = -1.5
    const Vec3d b{ 1.0f, 0.0f, -1.5f };  // behind the blocker on screen
    std::unique_ptr<IInteraction> drag =
        Tool().BeginDrag(Context(), view, PointerEvent{ .Position = PixelOf(view, a) });
    ASSERT_NE(drag, nullptr);
    drag->OnPointerMove(Context(), view, PointerEvent{ .Position = PixelOf(view, b) });
    drag->OnPointerUp(Context(), view, PointerEvent{ .Position = PixelOf(view, b) });
    ASSERT_EQ(Tool().GetPhase(), ClipPhase::Pending);
    const Plane plane = *Tool().GetClipPlane();
    EXPECT_NEAR(plane.SignedDistanceTo(a), 0.0f, 1e-3f);
    EXPECT_NEAR(plane.SignedDistanceTo(b), 0.0f, 1e-3f) << "the second point left the wall's plane";
    EXPECT_NEAR(plane.SignedDistanceTo(view.Camera.Position), 0.0f, 1e-3f);
    (void)blocker;
    Press(SDLK_ESCAPE);
}

TEST_F(ClipToolTest, ABrushSelectedThroughSeveralElementsIsClippedOnce)
{
    const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    SelectElements(brush, MeshElementKind::Face, { 0, 1, 2 });
    EditorViewport top = TopViewport();
    Tool().SetMode(Context(), ClipMode::Split);
    Draw(top, Vec3d{ 0, 0, -3 }, Vec3d{ 0, 0, 3 });
    Press(SDLK_RETURN);
    EXPECT_EQ(BrushCount(), 2u);
}

TEST_F(ClipToolTest, OneInvalidBrushBlocksTheWholeCommit)
{
    const EntityId good = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    const EntityId bad = AddBrush({ 0, 0, 4 }, { 1, 1, 1 });
    // A brush with a tunnel through it along X: a cut across the tunnel has an
    // annular cap, which the clip kernel cannot close, so its halves are not
    // solids.
    {
        const BrushMesh solid = *Scene().TryGetBrushMesh(bad);
        std::uint32_t face = 0;
        for (std::uint32_t i = 0; i < solid.Faces.size(); ++i)
            if (BrushComputeFaceNormal(solid, solid.Faces[i]).X > 0.99f)
                face = i;
        const BrushFaceFrame frame = *FaceFrame(solid, face, 1e-3f).Frame;
        Vec2d lo = frame.Outline.front(), hi = lo;
        for (const Vec2d& p : frame.Outline)
        {
            lo = Vec2d{ std::min(lo.X, p.X), std::min(lo.Y, p.Y) };
            hi = Vec2d{ std::max(hi.X, p.X), std::max(hi.Y, p.Y) };
        }
        const std::vector<Vec2d> window = CarveShapeOutline(
            CarveShape::Rectangle, Vec2d{ lo.X + 0.5f, lo.Y + 0.5f }, Vec2d{ hi.X - 0.5f, hi.Y - 0.5f }, {});
        const CarveOutcome pierced = CarveFacePolygonThrough(solid, face, frame, window, 1e-3f);
        ASSERT_TRUE(pierced.Ok()) << CarveStatusText(pierced.Status());
        Sink().PreviewMesh(bad, pierced.Value().Mesh);
    }
    Select({ SelectableRef::EntitySelection(Registry(), good), SelectableRef::EntitySelection(Registry(), bad) });
    EditorViewport top = TopViewport();
    Tool().SetMode(Context(), ClipMode::KeepFront);
    Draw(top, Vec3d{ 0, 0, -6 }, Vec3d{ 0, 0, 6 });
    EXPECT_FALSE(Tool().CanCommit());
    Press(SDLK_RETURN);
    EXPECT_EQ(Tool().GetPhase(), ClipPhase::Pending);
    Press(SDLK_ESCAPE);
    EXPECT_NEAR(BoundsOf(good).Min.X, -1.0f, kTol) << "the valid brush was not clipped alone";
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(ClipToolTest, ModeSurvivesAFocusChangeAndCommitStaysOnTheTool)
{
    const ZoneId second = AddSecondZone();
    ASSERT_TRUE(second.IsValid());
    Tool().SetMode(Context(), ClipMode::KeepBack);
    ASSERT_TRUE(Workspace.World.SetFocusZone(second));
    EXPECT_EQ(Tool().GetMode(), ClipMode::KeepBack);

    const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    SelectEntity(brush);
    ASSERT_TRUE(Workspace.Interaction.Tools->Activate("clip"));
    EditorViewport top = TopViewport();
    Draw(top, Vec3d{ 0, 0, -3 }, Vec3d{ 0, 0, 3 });
    Press(SDLK_RETURN);
    EXPECT_EQ(Workspace.Interaction.Tools->GetActiveTool()->GetId(), "clip");
}

TEST_F(ClipToolTest, AnUncappedClipIsOpenAlongTheCutAndStillCommits)
{
    const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    SelectEntity(brush);
    EditorViewport top = TopViewport();
    Tool().SetMode(Context(), ClipMode::KeepFront);
    Tool().SetCapped(Context(), false);
    Draw(top, Vec3d{ 0, 0, -3 }, Vec3d{ 0, 0, 3 });
    ASSERT_TRUE(Tool().CanCommit());
    Press(SDLK_RETURN);
    BrushMesh check = *Scene().TryGetBrushMesh(brush);
    EXPECT_EQ(check.Faces.size(), 5u);
    const BrushRepairResult report = BrushValidateAndRepair(check);
    EXPECT_TRUE(report.Ok);
    EXPECT_FALSE(report.Closed);
    Tool().SetCapped(Context(), true);
}
