#include "WorkspaceFixture.h"

#include "brush/BrushValidation.h"
#include "brush/BrushFaceFrame.h"
#include "brush/CarvePolygon.h"
#include "brush/CarveShape.h"
#include "document/tools/ClipTool.h"
#include "meshedit/ManipulationSink.h"
#include "render/PreviewBuffer.h"
#include "EditorTheme.h"
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
                               { BrushOps::Clip(original, plane, false) } });
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
                               { BrushOps::Clip(original, local, false) } });
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

TEST_F(ClipToolTest, APerspectiveLineOnAWallCutsStraightThroughIt)
{
    const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    SelectEntity(brush);
    EditorViewport view = PerspectiveViewport();
    Context().Grid.SnapEnabled = false; // the points are the test's own, not the lattice's
    Tool().SetMode(Context(), ClipMode::Split);
    // A diagonal across the brush's near face (z = 1), which the press lands
    // on; the plane stands through it along the face's normal, not the eye.
    const Vec3d a{ -0.6f, 0.9f, 1.0f };
    const Vec3d b{ 0.6f, 0.3f, 1.0f }; // off the view axis, so the eye is not on the plane by accident
    Draw(view, a, b);
    ASSERT_TRUE(Tool().CanCommit());
    const Plane before = *Tool().GetClipPlane();
    EXPECT_NEAR(before.SignedDistanceTo(a), 0.0f, 1e-3f);
    EXPECT_NEAR(before.SignedDistanceTo(b), 0.0f, 1e-3f);
    EXPECT_NEAR(before.SignedDistanceTo(a + Vec3d{ 0, 0, 1 }), 0.0f, 1e-3f) << "not along the face normal";
    EXPECT_GT(std::abs(before.SignedDistanceTo(view.Camera.Position)), 0.5f) << "through the eye";

    // The camera moving afterwards changes nothing: the plane is owned.
    const BrushMesh previewBefore = *Scene().TryGetBrushMesh(brush);
    view.Camera.Position = Vec3d(4.0f, 3.0f, -2.0f);
    view.Camera.Yaw = 0.7f;
    Press(SDLK_TAB);
    Press(SDLK_TAB);
    Press(SDLK_TAB);
    ASSERT_EQ(Tool().GetMode(), ClipMode::Split);
    const Plane after = *Tool().GetClipPlane();
    EXPECT_NEAR(before.Normal.Dot(after.Normal), 1.0f, kTol);
    EXPECT_NEAR(before.D, after.D, kTol);
    EXPECT_EQ(Scene().TryGetBrushMesh(brush)->Vertices.size(), previewBefore.Vertices.size());
    Press(SDLK_ESCAPE);
}

TEST_F(ClipToolTest, AShortDragPendsWithItsPinsInsteadOfVanishing)
{
    // Grid snap on, a drag inside one lattice cell on the near face: both pins
    // land on the same point. The gesture stays, pins and all, uncommittable
    // until a pin is dragged apart.
    const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    SelectEntity(brush);
    EditorViewport view = PerspectiveViewport();
    Context().Grid.SnapEnabled = true;
    Context().Grid.Spacing = 1.0f;
    Tool().SetMode(Context(), ClipMode::Split);
    Draw(view, Vec3d{ 0.1f, 0.1f, 1.0f }, Vec3d{ 0.3f, 0.2f, 1.0f });
    EXPECT_FALSE(Tool().CanCommit());
    EXPECT_EQ(Context().Overlay.PointHandles.size(), 2u) << "the pins are gone";

    // Drag the second pin to the next lattice point.
    std::unique_ptr<IInteraction> drag = Tool().BeginDrag(
        Context(), view, PointerEvent{ .Position = PixelOf(view, Vec3d{ 0, 0, 1 }) });
    ASSERT_NE(drag, nullptr) << "no pin under the cursor";
    drag->OnPointerMove(Context(), view, PointerEvent{ .Position = PixelOf(view, Vec3d{ 0, 1, 1 }) });
    drag->OnPointerUp(Context(), view, PointerEvent{ .Position = PixelOf(view, Vec3d{ 0, 1, 1 }) });
    EXPECT_TRUE(Tool().CanCommit());
    Press(SDLK_ESCAPE);
}

TEST_F(ClipToolTest, SplitPreviewsTheWholeBrushWithItsSection)
{
    const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    const BrushMesh original = *Scene().TryGetBrushMesh(brush);
    SelectEntity(brush);
    EditorViewport top = TopViewport();
    for (const bool capped : { true, false })
    {
        Tool().SetMode(Context(), ClipMode::Split);
        Tool().SetCapped(Context(), capped);
        Draw(top, Vec3d{ 0, 0, -3 }, Vec3d{ 0, 0, 3 });
        EXPECT_EQ(Scene().TryGetBrushMesh(brush)->Faces.size(), original.Faces.size()) << "a half went missing";
        const std::optional<PreviewMesh>& wire = Context().Preview.GetMesh();
        ASSERT_TRUE(wire.has_value());
        EXPECT_EQ(wire->Mesh.Faces.size(), 1u) << "the section, and only the section";
        EXPECT_EQ(wire->Color, EditorTheme::Readout);
        Press(SDLK_ESCAPE);
    }
    Tool().SetCapped(Context(), true);

    Tool().SetMode(Context(), ClipMode::KeepFront);
    Draw(top, Vec3d{ 0, 0, -3 }, Vec3d{ 0, 0, 3 });
    const std::optional<PreviewMesh>& discarded = Context().Preview.GetMesh();
    ASSERT_TRUE(discarded.has_value());
    EXPECT_EQ(discarded->Color, EditorTheme::ContextZoneDim);
    EXPECT_GT(discarded->Mesh.Faces.size(), 1u) << "the whole discarded half";
    Press(SDLK_ESCAPE);
    EXPECT_FALSE(Context().Preview.GetMesh().has_value());
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
    // A brush with a face bent out of plane cannot be split in that face's
    // plane, so the kernel refuses it.
    {
        BrushMesh warped = *Scene().TryGetBrushMesh(bad);
        std::uint32_t top = 0;
        for (std::uint32_t i = 0; i < warped.Faces.size(); ++i)
            if (BrushComputeFaceNormal(warped, warped.Faces[i]).Y > 0.99f)
                top = i;
        warped.Vertices[warped.Faces[top].Loop.front()].Position.Y += 0.4f;
        Sink().PreviewMesh(bad, warped);
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

namespace
{
// A doorway pierced through a box's +X wall, standing on the floor edge: a
// horizontal cut through it leaves two jambs below and one lintel above.
BrushMesh DoorwayMesh(const BrushMesh& box)
{
    std::uint32_t face = 0;
    for (std::uint32_t i = 0; i < box.Faces.size(); ++i)
        if (BrushComputeFaceNormal(box, box.Faces[i]).X > 0.99f)
            face = i;
    const BrushFaceFrame frame = *FaceFrame(box, face, 1e-3f).Frame;
    Vec2d lo = frame.Outline.front(), hi = lo;
    for (const Vec2d& p : frame.Outline)
    {
        lo = Vec2d{ std::min(lo.X, p.X), std::min(lo.Y, p.Y) };
        hi = Vec2d{ std::max(hi.X, p.X), std::max(hi.Y, p.Y) };
    }
    const std::vector<Vec2d> door = CarveShapeOutline(
        CarveShape::Rectangle, Vec2d{ lo.X + 0.5f, lo.Y }, Vec2d{ hi.X - 0.5f, lo.Y + 1.5f }, {});
    const CarveOutcome pierced = CarveFacePolygonThrough(box, face, frame, door, 1e-3f);
    EXPECT_TRUE(pierced.Ok()) << CarveStatusText(pierced.Status());
    return pierced.Ok() ? pierced.Value().Mesh : box;
}
}

TEST_F(ClipToolTest, ACutThroughADoorwayMakesOneBrushPerPiece)
{
    // Keep below: the two jambs, two brushes. Split: jambs and lintel, three.
    for (const ClipMode mode : { ClipMode::KeepBack, ClipMode::Split, ClipMode::KeepFront })
    {
        Workspace.World.NewWorld("doorway");
        const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
        Sink().PreviewMesh(brush, DoorwayMesh(*Scene().TryGetBrushMesh(brush)));
        SelectEntity(brush);
        // Seen from above, a line along Z at x = 0... no: the cut is horizontal,
        // so it is drawn in a Front view, where the grid plane's normal is Z
        // and a line along X stands a plane along Z through it. Use the plane
        // directly: a Front viewport's line at y = 0.
        EditorViewport front;
        front.ApplyOrientation(ViewportOrientation::Front);
        front.Id = ViewportId{ 3 };
        front.RegionMin = ImVec2(0.0f, 0.0f);
        front.RegionMax = ImVec2(400.0f, 400.0f);
        Tool().SetMode(Context(), mode);
        Draw(front, Vec3d{ -3, 0, 0 }, Vec3d{ 3, 0, 0 });
        ASSERT_TRUE(Tool().CanCommit());
        const Plane plane = *Tool().GetClipPlane();
        const bool frontIsUp = plane.Normal.Y > 0.0f;
        Press(SDLK_RETURN);
        const std::size_t expected = mode == ClipMode::Split ? 3u : ((mode == ClipMode::KeepFront) == frontIsUp ? 1u : 2u);
        EXPECT_EQ(BrushCount(), expected) << "mode " << static_cast<int>(mode);
        EXPECT_EQ(Workspace.Selection.GetSelection().size(), expected);
        for (const EntityId entity : Scene().GetAllEntities())
            if (const BrushMesh* mesh = Scene().TryGetBrushMesh(entity))
            {
                BrushMesh copy = *mesh;
                EXPECT_TRUE(BrushValidateAndRepair(copy).Closed);
                EXPECT_EQ(BrushConnectedComponents(*mesh).size(), 1u) << "a brush holds one solid";
            }
        Commands.Undo();
        EXPECT_EQ(BrushCount(), 1u) << "one step";
    }
}
