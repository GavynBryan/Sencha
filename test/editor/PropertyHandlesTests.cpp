#include "WorkspaceFixture.h"

#include "authoring/ComponentPropertySource.h"
#include "authoring/EditorComponentAdapter.h"
#include "input/InputRouter.h"
#include "viewport/EditorViewport.h"
#include "viewport/ViewportProjection.h"
#include "workspace/PropertyHandles.h"

#include <render/PointLightComponent.h>

#include <gtest/gtest.h>

// A selected light's range is a ring around it with a knob: dragging the knob
// sets the range to the pointer's distance, as one step, and Escape puts it
// back. Its intensity changes by scrolling over it, one step once the wheel
// rests.
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
    EXPECT_FALSE(marks.Rings.empty());
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
