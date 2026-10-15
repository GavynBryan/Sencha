#include "WorkspaceFixture.h"

#include "authoring/ComponentPropertySource.h"
#include "authoring/EditorComponentAdapter.h"
#include "input/InputRouter.h"
#include "viewport/EditorViewport.h"
#include "viewport/ViewportProjection.h"
#include "workspace/PropertyHandles.h"

#include <components/CameraComponent.h>
#include <math/Angle.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

// A selected light's range is a ring around it with a knob: dragging the knob
// sets the range to the pointer's distance, as one step, and Escape puts it
// back. Its intensity changes by scrolling over it, one step once the wheel
// rests. A spot light's cone and a camera's field of view are angles with a
// knob on an edge.
namespace
{
class PropertyHandlesTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Light = Scene().CreateEntity(Vec3d{});
        PointLightComponent light;
        light.Range = 4.0f;
        Scene().GetRegistry().Components.AddComponent(Light, light);
        SelectEntity(Light);
        for (const std::unique_ptr<EditorViewport>& viewport : Workspace.Layout.All())
            viewport->RegionMin = viewport->RegionMax = Vec2d{};
        EditorViewport& top = *Workspace.Layout.All()[0];
        top.ApplyOrientation(ViewportOrientation::Top);
        top.RegionMin = Vec2d(0.0f, 0.0f);
        top.RegionMax = Vec2d(400.0f, 400.0f);
        Top = top.Id;
        Router.AddHandler([this](const InputEvent& e, PointerCapture& cap) { return Handles.OnInput(e, cap); });
    }

    [[nodiscard]] Vec2d PixelOf(Vec3d world) const
    {
        return ViewportProjection(*Workspace.Layout.Find(Top)).WorldToPixel(world)->Pixel;
    }

    [[nodiscard]] const PointLightComponent& Lit() { return *Scene().GetRegistry().Components.TryGet<PointLightComponent>(Light); }

    void Drag(Vec3d from, Vec3d to)
    {
        (void)Router.Route(PointerDownEvent{ .Position = PixelOf(from), .Button = MouseButton::Left, .Modifiers = {},
                                             .Viewport = Top });
        (void)Router.Route(PointerMoveEvent{ .Position = PixelOf(to), .Delta = {}, .Modifiers = {}, .Viewport = Top });
    }

    EntityId Light{};
    ViewportId Top{};
    ComponentPropertySource Components{ Workspace.World, Workspace.Selection, Commands,
                                        Workspace.Affordances->Registry() };
    PropertyHandles Handles{ Workspace, Components };
    InputRouter Router;
};
} // namespace

TEST_F(PropertyHandlesTest, ARadiusIsARingWithAKnobInEveryShownView)
{
    Handles.Publish();
    const PropertyHandleMarks& marks = Workspace.Interaction.Overlay.Handles;
    ASSERT_EQ(marks.Knobs.size(), 1u) << "one knob in the one view shown";
    EXPECT_EQ(marks.Knobs[0].Viewport, Top);
    EXPECT_NEAR((marks.Knobs[0].World - Vec3d(4.0f, 0.0f, 0.0f)).Magnitude(), 0.0f, 1e-3f);
    EXPECT_FALSE(marks.Strokes.empty());
    EXPECT_TRUE(marks.Values.empty()) << "no value shows while nothing changes";
}

TEST_F(PropertyHandlesTest, DraggingTheKnobSetsTheRadiusAsOneStep)
{
    Handles.Publish();
    Drag(Vec3d(4.0f, 0.0f, 0.0f), Vec3d(6.0f, 0.0f, 0.0f));
    EXPECT_NEAR(Lit().Range, 6.0f, 0.1f) << "previewed live while dragging";
    Handles.Publish();
    EXPECT_FALSE(Workspace.Interaction.Overlay.Handles.Values.empty()) << "the value shows while it changes";
    (void)Router.Route(PointerUpEvent{ .Position = PixelOf(Vec3d(6.0f, 0.0f, 0.0f)), .Button = MouseButton::Left,
                                       .Modifiers = {}, .Viewport = Top });
    EXPECT_NEAR(Lit().Range, 6.0f, 0.1f);
    ASSERT_TRUE(Commands.CanUndo());
    Commands.Undo();
    EXPECT_FLOAT_EQ(Lit().Range, 4.0f);
}

TEST_F(PropertyHandlesTest, EscapePutsAnUnfinishedDragBack)
{
    Handles.Publish();
    Drag(Vec3d(4.0f, 0.0f, 0.0f), Vec3d(8.0f, 0.0f, 0.0f));
    ASSERT_GT(Lit().Range, 7.0f);
    (void)Router.Route(KeyDownEvent{ .Key = SDLK_ESCAPE, .Modifiers = {} });
    EXPECT_FLOAT_EQ(Lit().Range, 4.0f);
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(PropertyHandlesTest, ScrollingOverTheLightChangesItsIntensityAsOneStepOnceTheWheelRests)
{
    const float before = Lit().Intensity;
    Handles.Publish();
    (void)Router.Route(PointerMoveEvent{ .Position = PixelOf(Vec3d{}), .Delta = {}, .Modifiers = {}, .Viewport = Top });
    (void)Router.Route(WheelEvent{ .Position = {}, .Delta = 1.0f, .Modifiers = {} });
    (void)Router.Route(WheelEvent{ .Position = {}, .Delta = 1.0f, .Modifiers = {} });
    EXPECT_GT(Lit().Intensity, before) << "previewed live while the wheel turns";
    for (int frame = 0; frame < 30; ++frame)
        Handles.Publish();
    ASSERT_TRUE(Commands.CanUndo());
    Commands.Undo();
    EXPECT_FLOAT_EQ(Lit().Intensity, before) << "both notches are one step";
}

namespace
{
Vec3d Toward(float degrees, Vec3d axis, Vec3d across, float length)
{
    const float radians = ToRadians(degrees);
    return (axis * std::cos(radians) + across * std::sin(radians)) * length;
}
} // namespace

TEST_F(PropertyHandlesTest, DraggingAConesKnobSetsItsAngleAsOneStep)
{
    const EntityId spot = Scene().CreateEntity(Vec3d{});
    SpotLightComponent light;
    light.Range = 4.0f;
    Scene().GetRegistry().Components.AddComponent(spot, light);
    SelectEntity(spot);
    const auto lit = [&] { return *Scene().GetRegistry().Components.TryGet<SpotLightComponent>(spot); };
    // From above, the light points along -Z and its cone opens either side of
    // it along X: the inner angle's knob on one edge, the outer's on the other.
    const Vec3d forward(0.0f, 0.0f, -1.0f);

    Handles.Publish();
    const PropertyHandleMarks& marks = Workspace.Interaction.Overlay.Handles;
    ASSERT_EQ(marks.Knobs.size(), 3u) << "range, inner and outer";
    const float outerDepth = Toward(35.0f, forward, Vec3d{}, 4.0f).Z;
    const auto outer = std::ranges::find_if(marks.Knobs, [&](const PropertyKnobRequest& knob) {
        return std::abs(knob.World.Magnitude() - 4.0f) < 1e-3f && std::abs(knob.World.Z - outerDepth) < 1e-3f;
    });
    ASSERT_NE(outer, marks.Knobs.end()) << "the outer angle's knob ends its edge, as far as the light reaches";
    const Vec3d outerKnob = outer->World;
    const Vec3d outerEdge(outerKnob.X > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);

    Drag(outerKnob, Toward(50.0f, forward, outerEdge, 2.0f));
    EXPECT_NEAR(lit().OuterAngleDegrees, 50.0f, 0.5f) << "the angle is the pointer's, however far out";
    EXPECT_FLOAT_EQ(lit().InnerAngleDegrees, 25.0f);
    (void)Router.Route(PointerUpEvent{ .Position = PixelOf(Toward(50.0f, forward, outerEdge, 2.0f)),
                                       .Button = MouseButton::Left, .Modifiers = {}, .Viewport = Top });
    ASSERT_TRUE(Commands.CanUndo());
    Commands.Undo();
    EXPECT_FLOAT_EQ(lit().OuterAngleDegrees, 35.0f);
}

TEST_F(PropertyHandlesTest, DraggingACamerasKnobSetsItsVerticalFieldOfView)
{
    const EntityId eye = Scene().CreateEntity(Vec3d{});
    Scene().GetRegistry().Components.AddComponent(eye, CameraComponent{});
    SelectEntity(eye);
    // Its field of view opens upward and downward, so it is seen from the side.
    Workspace.Layout.Find(Top)->ApplyOrientation(ViewportOrientation::Right);

    Handles.Publish();
    const PropertyHandleMarks& marks = Workspace.Interaction.Overlay.Handles;
    ASSERT_EQ(marks.Knobs.size(), 1u);
    EXPECT_GT(marks.Knobs[0].World.Y, 0.0f) << "the knob is on the upper edge";
    EXPECT_FALSE(marks.Strokes.empty());

    const Vec3d to = Toward(45.0f, Vec3d(0.0f, 0.0f, -1.0f), Vec3d(0.0f, 1.0f, 0.0f), 3.0f);
    Drag(marks.Knobs[0].World, to);
    (void)Router.Route(PointerUpEvent{ .Position = PixelOf(to), .Button = MouseButton::Left, .Modifiers = {},
                                       .Viewport = Top });
    const CameraComponent& camera = *Scene().GetRegistry().Components.TryGet<CameraComponent>(eye);
    EXPECT_NEAR(camera.FovYRadians, std::numbers::pi_v<float> / 2.0f, 0.01f) << "45 degrees either side is 90";
}

// The workspace's lifecycle ends a handle's change wherever it was begun:
// Escape and teardown cancel it for good, save commits it, a focus change puts
// the old zone back as it was.
TEST_F(PropertyHandlesTest, AScrollTheWorkspaceCancelsDoesNotCommitOnceTheWheelRests)
{
    const float before = Lit().Intensity;
    Handles.Publish();
    (void)Router.Route(PointerMoveEvent{ .Position = PixelOf(Vec3d{}), .Delta = {}, .Modifiers = {}, .Viewport = Top });
    (void)Router.Route(WheelEvent{ .Position = {}, .Delta = 1.0f, .Modifiers = {} });
    ASSERT_GT(Lit().Intensity, before);

    Workspace.CancelOpenEdits();
    EXPECT_FLOAT_EQ(Lit().Intensity, before);
    for (int frame = 0; frame < 30; ++frame)
        Handles.Publish();
    EXPECT_FLOAT_EQ(Lit().Intensity, before) << "the rested wheel committed what had been cancelled";
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(PropertyHandlesTest, ADragTheWorkspaceCancelsLetsGoOfThePointer)
{
    Handles.Publish();
    Drag(Vec3d(4.0f, 0.0f, 0.0f), Vec3d(8.0f, 0.0f, 0.0f));
    ASSERT_TRUE(Router.PointerCaptured());
    Workspace.CancelOpenEdits();
    EXPECT_FLOAT_EQ(Lit().Range, 4.0f);
    EXPECT_FALSE(Router.PointerCaptured()) << "a cancelled drag kept every later pointer event";
}

TEST_F(PropertyHandlesTest, SavingMidDragCommitsWhatIsShownAsOneStep)
{
    Handles.Publish();
    Drag(Vec3d(4.0f, 0.0f, 0.0f), Vec3d(6.0f, 0.0f, 0.0f));
    Workspace.ResolvePendingEdits();
    EXPECT_NEAR(Lit().Range, 6.0f, 0.1f);
    EXPECT_FALSE(Router.PointerCaptured());
    ASSERT_TRUE(Commands.CanUndo());
    Commands.Undo();
    EXPECT_FLOAT_EQ(Lit().Range, 4.0f);
}

TEST_F(PropertyHandlesTest, AFocusChangeMidScrollLeavesTheOldZoneAsItWas)
{
    const float before = Lit().Intensity;
    EditorDocument& first = Workspace.ActiveDocument();
    const ZoneId second = AddSecondZone();
    Handles.Publish();
    (void)Router.Route(PointerMoveEvent{ .Position = PixelOf(Vec3d{}), .Delta = {}, .Modifiers = {}, .Viewport = Top });
    (void)Router.Route(WheelEvent{ .Position = {}, .Delta = 1.0f, .Modifiers = {} });
    ASSERT_GT(Lit().Intensity, before);

    ASSERT_TRUE(Workspace.World.SetFocusZone(second));
    const auto* light = first.GetScene().GetRegistry().Components.TryGet<PointLightComponent>(Light);
    ASSERT_NE(light, nullptr);
    EXPECT_FLOAT_EQ(light->Intensity, before) << "the preview stayed in the zone it was begun in";
    EXPECT_FALSE(first.IsDirty());
}

// A sheet's edit stays in the document it began in when focus moves under it.
TEST_F(PropertyHandlesTest, AnEditBegunInOneZoneIsNotWrittenIntoAnother)
{
    const ZoneId second = AddSecondZone();
    const std::string key = Components.KeyOf(ResolveComponentTypeId<PointLightComponent>(), "intensity");
    ASSERT_TRUE(Components.BeginEdit(key));
    ASSERT_TRUE(Workspace.World.SetFocusZone(second));
    EXPECT_EQ(Components.PreviewEdit(key, "9"), PropertyEditResult::Gone);
    EXPECT_FALSE(Components.HasOpenEdit());
}
