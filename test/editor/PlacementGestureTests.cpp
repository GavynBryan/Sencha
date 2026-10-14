#include "PrefabSourceFixture.h"
#include "WorkspaceFixture.h"

#include "document/placement/PlacementSession.h"
#include "input/InputRouter.h"
#include "input/ViewportToolDispatcher.h"
#include "interaction/InteractionHost.h"
#include "tools/ToolRegistry.h"
#include "viewport/EditorViewport.h"
#include "viewport/ViewportProjection.h"

#include <world/identity/PersistentIdComponent.h>
#include <world/transform/TransformComponents.h>

#include <SDL3/SDL_keycode.h>

#include <cmath>
#include <cstring>
#include <numbers>

// The Entity tool's placements, driven through the views' dispatcher as the
// pointer drives them: hover shows the placement pending, a click makes
// exactly that, a press dragged away turns it, and a prefab carried in from
// the shelf is pending wherever the pointer is until it is let go. Every way
// a gesture can end without its release places nothing.
namespace
{
class PlacementGestureTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Workspace.ActiveDocument().SetContentRoots({ Prefabs.Root });
        for (const std::unique_ptr<EditorViewport>& viewport : Workspace.Layout.All())
            viewport->RegionMin = viewport->RegionMax = Vec2d{};
        EditorViewport& top = *Workspace.Layout.All()[0];
        top.ApplyOrientation(ViewportOrientation::Top);
        top.RegionMin = Vec2d(0.0f, 0.0f);
        top.RegionMax = Vec2d(400.0f, 400.0f);
        Top = top.Id;
        Router.AddHandler([this](const InputEvent& e, PointerCapture& capture) {
            return Workspace.Interaction.Dispatcher->OnInput(e, capture);
        });
        Hold(Prefabs.Door);
        ASSERT_TRUE(Workspace.Interaction.Tools->Activate("place"));
    }

    void Hold(const std::string& prefab) { Workspace.Resources.SetActive(AssetRef{ AssetType::Scene, prefab }); }

    [[nodiscard]] Vec2d PixelOf(Vec3d world) const
    {
        return ViewportProjection(*Workspace.Layout.Find(Top)).WorldToPixel(world)->Pixel;
    }

    void Move(Vec2d pixel, ViewportId view)
    {
        (void)Router.Route(PointerMoveEvent{ .Position = pixel, .Delta = {}, .Modifiers = {}, .Viewport = view });
    }
    void Move(Vec3d world) { Move(PixelOf(world), Top); }
    void Press(Vec2d pixel)
    {
        (void)Router.Route(
            PointerDownEvent{ .Position = pixel, .Button = MouseButton::Left, .Modifiers = {}, .Viewport = Top });
    }
    void Press(Vec3d world) { Press(PixelOf(world)); }
    void Release(Vec2d pixel, ViewportId view)
    {
        (void)Router.Route(
            PointerUpEvent{ .Position = pixel, .Button = MouseButton::Left, .Modifiers = {}, .Viewport = view });
    }
    void Release(Vec3d world) { Release(PixelOf(world), Top); }
    void Hand(Vec3d world)
    {
        (void)Router.Route(PointerHandedEvent{ .Position = PixelOf(world), .Modifiers = {}, .Viewport = Top });
    }
    void Escape() { (void)Router.Route(KeyDownEvent{ .Key = SDLK_ESCAPE, .Modifiers = {} }); }

    [[nodiscard]] PlacementSession& Session() { return Workspace.Interaction.Placement; }

    // The root of the one placement made, by the selection a placement leaves.
    [[nodiscard]] const Transform3f* PlacedRoot()
    {
        const EntityId root = Workspace.Selection.GetPrimarySelection().Entity;
        return root.IsValid() ? Scene().TryGetLocalTransform(root) : nullptr;
    }

    [[nodiscard]] static Vec3d FacingOf(const Transform3f& placement)
    {
        return placement.Rotation.RotateVector(Vec3d::Forward());
    }

    LoggingProvider PrefabLogging;
    PrefabSourceFixture Prefabs{ PrefabLogging };
    InputRouter Router;
    ViewportId Top{};
    const Vec3d Spot{ 2.0f, 0.0f, 3.0f };
    const Vec3d East{ 9.0f, 0.0f, 3.0f };
};

[[nodiscard]] bool SameBits(const Transform3f& a, const Transform3f& b)
{
    return std::memcmp(&a, &b, sizeof(Transform3f)) == 0;
}

void ExpectNear(Vec3d actual, Vec3d expected)
{
    EXPECT_NEAR(actual.X, expected.X, 1e-4f);
    EXPECT_NEAR(actual.Y, expected.Y, 1e-4f);
    EXPECT_NEAR(actual.Z, expected.Z, 1e-4f);
}
} // namespace

TEST_F(PlacementGestureTest, HoveringPendsThePrefabWhereThePointerPoints)
{
    Move(Spot);
    ASSERT_EQ(Session().Phase(), PlacementPhase::Aiming);
    EXPECT_EQ(Session().Pending()->Subject.Key, Prefabs.Door);
    ExpectNear(Session().Pending()->Site.Point, Spot);
    EXPECT_FALSE(Workspace.ActiveDocument().IsDirty()) << "a pending placement is not in the document";
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(PlacementGestureTest, AClickMakesExactlyWhatWasShown)
{
    Move(Spot);
    const Transform3f shown = Session().Pending()->Placement;
    Press(Spot);
    Release(Spot);

    const Transform3f* placed = PlacedRoot();
    ASSERT_NE(placed, nullptr);
    EXPECT_TRUE(SameBits(*placed, shown));
    EXPECT_EQ(Session().Phase(), PlacementPhase::Idle);

    Commands.Undo();
    EXPECT_FALSE(Workspace.Selection.GetPrimarySelection().Entity.IsValid());
}

TEST_F(PlacementGestureTest, AClickIsSettledAtItsPressNotItsRelease)
{
    Move(East);
    Press(Spot); // no move between: the press catches the hover up
    Release(PixelOf(Spot) + Vec2d(3.0f, 0.0f), Top);
    const Transform3f* placed = PlacedRoot();
    ASSERT_NE(placed, nullptr);
    ExpectNear(placed->Position, Spot);
}

TEST_F(PlacementGestureTest, AHeldButtonIsNotAHover)
{
    Workspace.Grid.SnapEnabled = false; // so a pixel's drift would show
    Move(Spot);
    Press(Spot);
    const Vec3d pressed = Session().Pending()->Site.Point;
    Move(PixelOf(Spot) + Vec2d(2.0f, 1.0f), Top);
    EXPECT_EQ(Session().Pending()->Site.Point, pressed);
    EXPECT_EQ(Session().Phase(), PlacementPhase::Aiming);
}

TEST_F(PlacementGestureTest, AReleaseThatEndsNoGestureCancelsNothing)
{
    Move(Spot);
    Release(Spot); // its press went elsewhere, or was already aborted
    EXPECT_EQ(Session().Phase(), PlacementPhase::Aiming) << "a stray release cancelled the tool";
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(PlacementGestureTest, DraggingFromTheSpotTurnsItToFaceThePointer)
{
    Move(Spot);
    Press(Spot);
    Move(East);
    ASSERT_EQ(Session().Phase(), PlacementPhase::Turning);
    ExpectNear(FacingOf(Session().Pending()->Placement), Vec3d(1.0f, 0.0f, 0.0f));
    ExpectNear(Session().Pending()->Site.Point, Spot);
    ASSERT_TRUE(Workspace.Interaction.Overlay.Readout.Active());

    const Transform3f shown = Session().Pending()->Placement;
    Release(East);
    const Transform3f* placed = PlacedRoot();
    ASSERT_NE(placed, nullptr);
    EXPECT_TRUE(SameBits(*placed, shown));
    EXPECT_FALSE(Workspace.Interaction.Overlay.Readout.Active());

    // The next placement starts facing the same way.
    Move(Vec3d(-4.0f, 0.0f, -4.0f));
    ExpectNear(FacingOf(Session().Pending()->Placement), Vec3d(1.0f, 0.0f, 0.0f));
}

TEST_F(PlacementGestureTest, ATurnIsOneStepThatUndoesAndRedoesAsItself)
{
    Move(Spot);
    Press(Spot);
    Move(East);
    Release(East);
    const EntityId root = Workspace.Selection.GetPrimarySelection().Entity;
    ASSERT_TRUE(root.IsValid());
    const auto identity = *Scene().GetRegistry().Components.TryGet<PersistentIdComponent>(root);

    Commands.Undo();
    EXPECT_FALSE(Commands.CanUndo()) << "the turn was more than one step";
    Commands.Redo();
    const EntityId again = Workspace.Selection.GetPrimarySelection().Entity;
    ASSERT_TRUE(again.IsValid());
    EXPECT_EQ(Scene().GetRegistry().Components.TryGet<PersistentIdComponent>(again)->Id, identity.Id);
}

TEST_F(PlacementGestureTest, WithSnapOnATurnLandsOnTheStepAndWithItOffItIsFree)
{
    const Vec3d offAxis = Spot + Vec3d(6.0f, 0.0f, 2.0f);
    const float step = std::numbers::pi_v<float> / 4.0f;

    Workspace.Grid.SnapEnabled = true;
    Move(Spot);
    Press(Spot);
    Move(offAxis);
    const float snapped = Session().Pending()->Yaw;
    EXPECT_NEAR(std::remainder(snapped, step), 0.0f, 1e-4f);
    Escape();

    Workspace.Grid.SnapEnabled = false;
    Move(Spot);
    Press(Spot);
    Move(offAxis);
    const Vec3d toward = (offAxis - Spot).Normalized();
    ExpectNear(FacingOf(Session().Pending()->Placement), toward);
}

TEST_F(PlacementGestureTest, EscapeMidTurnPlacesNothing)
{
    Move(Spot);
    Press(Spot);
    Move(East);
    Escape();
    EXPECT_NE(Session().Phase(), PlacementPhase::Turning);
    EXPECT_FALSE(Workspace.Interaction.Interactions.IsActive());
    Release(East);
    EXPECT_FALSE(Commands.CanUndo());
    EXPECT_FALSE(PlacedRoot());
}

TEST_F(PlacementGestureTest, LosingFocusMidTurnPlacesNothing)
{
    Move(Spot);
    Press(Spot);
    Move(East);
    (void)Router.Route(FocusLostEvent{});
    EXPECT_FALSE(Workspace.Interaction.Interactions.IsActive());
    EXPECT_FALSE(Router.PointerCaptured());
    Release(East);
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(PlacementGestureTest, LosingFocusMidPressPlacesNothing)
{
    Move(Spot);
    Press(Spot);
    (void)Router.Route(FocusLostEvent{});
    Release(Spot);
    EXPECT_FALSE(Commands.CanUndo()) << "the release after a focus loss clicked";
}

TEST_F(PlacementGestureTest, ATurnReleasedOutsideItsViewPlacesItAsLastShown)
{
    Move(Spot);
    Press(Spot);
    Move(East);
    const Transform3f shown = Session().Pending()->Placement;
    // Captured to its view, the release arrives stamped with it, far outside.
    Release(Vec2d(-250.0f, 900.0f), Top);
    const Transform3f* placed = PlacedRoot();
    ASSERT_NE(placed, nullptr);
    EXPECT_TRUE(SameBits(*placed, shown));
}

TEST_F(PlacementGestureTest, SwitchingToolsMidPressEndsTheGesture)
{
    Move(Spot);
    Press(Spot);
    ASSERT_TRUE(Workspace.Interaction.Tools->Activate("select"));
    Move(East);
    EXPECT_FALSE(Workspace.Interaction.Interactions.IsActive()) << "the new tool was handed the old press";
    Release(East);
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(PlacementGestureTest, SwitchingToolsMidTurnPlacesNothing)
{
    Move(Spot);
    Press(Spot);
    Move(East);
    ASSERT_TRUE(Workspace.Interaction.Tools->Activate("select"));
    EXPECT_EQ(Session().Phase(), PlacementPhase::Idle);
    Release(East);
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(PlacementGestureTest, ARebuiltEditingStackEndsATurnInFlight)
{
    Move(Spot);
    Press(Spot);
    Move(East);
    ASSERT_TRUE(Workspace.Interaction.Interactions.IsActive());
    Workspace.ResetInteractionState();
    EXPECT_FALSE(Workspace.Interaction.Interactions.IsActive()) << "a turn outlived the stack it was begun on";
    EXPECT_EQ(Session().Phase(), PlacementPhase::Idle);
    Release(East);
    EXPECT_FALSE(Commands.CanUndo());
    EXPECT_FALSE(Workspace.ActiveDocument().IsDirty());
}

TEST_F(PlacementGestureTest, APrefabTakenUpWhileAimingIsPendingAtOnce)
{
    Move(Spot);
    Hold(Prefabs.Crate);
    ASSERT_EQ(Session().Phase(), PlacementPhase::Aiming);
    EXPECT_EQ(Session().Pending()->Subject.Key, Prefabs.Crate);
    ExpectNear(Session().Pending()->Site.Point, Spot);
}

TEST_F(PlacementGestureTest, APrefabTakenUpMidTurnWaitsForTheTurnToEnd)
{
    Move(Spot);
    Press(Spot);
    Move(East);
    Hold(Prefabs.Crate);
    EXPECT_EQ(Session().Pending()->Subject.Key, Prefabs.Door);
    Release(East);
    ASSERT_NE(PlacedRoot(), nullptr);
    EXPECT_EQ(Workspace.ActiveDocument().SceneInstanceSourceOf(Workspace.Selection.GetPrimarySelection().Entity),
              Prefabs.Door);
    Move(Spot);
    EXPECT_EQ(Session().Pending()->Subject.Key, Prefabs.Crate);
}

TEST_F(PlacementGestureTest, APrefabThatWillNotPlaceIsRefusedOnClick)
{
    Hold("asset://props/nowhere.sscene");
    Move(Spot);
    ASSERT_NE(Session().Pending(), nullptr);
    EXPECT_FALSE(Session().Pending()->Refusal.empty());
    EXPECT_EQ(Workspace.Interaction.Tools->GetActiveTool()->Hint(Workspace.Interaction.Tools->GetContext()).rfind(
                  "Can't place this", 0),
              0u);
    Press(Spot);
    Release(Spot);
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(PlacementGestureTest, ACarryIsPendingWhereThePointerIsAndPlacesOnRelease)
{
    ASSERT_TRUE(Workspace.Interaction.Tools->Activate("select"));
    ASSERT_TRUE(Workspace.Interaction.Tools->Activate("place"));
    Hand(Spot);
    ASSERT_EQ(Session().Phase(), PlacementPhase::Aiming);
    ExpectNear(Session().Pending()->Site.Point, Spot);
    EXPECT_TRUE(Router.PointerCaptured());

    Move(East);
    ExpectNear(Session().Pending()->Site.Point, East);
    const Transform3f shown = Session().Pending()->Placement;
    Release(East);
    const Transform3f* placed = PlacedRoot();
    ASSERT_NE(placed, nullptr);
    EXPECT_TRUE(SameBits(*placed, shown));
    Commands.Undo();
    EXPECT_FALSE(Commands.CanUndo()) << "a carry was more than one step";
}

TEST_F(PlacementGestureTest, ACarriedPrefabIsPlacedWhicheverEntityWasChosen)
{
    Workspace.Interaction.Tools->GetActiveTool()->SelectVariant(Workspace.Interaction.Tools->GetContext(), 1);
    Hand(Spot);
    ASSERT_NE(Session().Pending(), nullptr);
    EXPECT_EQ(Session().Pending()->Subject.Key, Prefabs.Door);
}

TEST_F(PlacementGestureTest, ACarryOffTheViewsIsPendingNowhereAndLettingGoThereCancels)
{
    Hand(Spot);
    Move(Vec2d(-50.0f, -50.0f), ViewportId{});
    EXPECT_EQ(Session().Phase(), PlacementPhase::Idle);
    EXPECT_TRUE(Workspace.Interaction.Interactions.IsActive()) << "leaving the views is not the end of a carry";

    Move(Spot);
    EXPECT_EQ(Session().Phase(), PlacementPhase::Aiming) << "coming back resumes it";
    Move(Vec2d(-50.0f, -50.0f), ViewportId{});
    Release(Vec2d(-50.0f, -50.0f), ViewportId{});
    EXPECT_FALSE(Commands.CanUndo());
    EXPECT_FALSE(Workspace.Interaction.Interactions.IsActive());
}

TEST_F(PlacementGestureTest, EscapeMidCarryPlacesNothing)
{
    Hand(Spot);
    Move(East);
    Escape();
    EXPECT_EQ(Session().Phase(), PlacementPhase::Idle);
    Release(East);
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(PlacementGestureTest, AComponentTurnsAndPlacesLikeAPrefab)
{
    ToolRegistry& tools = *Workspace.Interaction.Tools;
    ITool& place = *tools.GetActiveTool();
    std::size_t light = 0;
    for (std::size_t i = 0; i < place.GetVariants().size(); ++i)
        if (place.GetVariants()[i].Label == "Spot Light")
            light = i;
    place.SelectVariant(tools.GetContext(), light);
    Move(Spot);
    Press(Spot);
    Move(East);
    Release(East);
    const Transform3f* placed = PlacedRoot();
    ASSERT_NE(placed, nullptr);
    ExpectNear(FacingOf(*placed), Vec3d(1.0f, 0.0f, 0.0f));
}
