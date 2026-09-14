#include "WorkspaceFixture.h"

#include "editmodes/ManipulatorSession.h"
#include "input/InputRouter.h"
#include "tools/RadialMenuSession.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolRegistryMenuModel.h"
#include "editmodes/TransformModeMenuModel.h"

#include <SDL3/SDL_keycode.h>

// Two wheels on one router, composed as the editor composes them: the tool
// wheel on Q ahead of the gizmo wheel on Z. Neither knows the other exists;
// an open wheel is modal, which is what keeps the other closed.
namespace
{
class RadialMenuRoutingTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        const auto frame = [] { return RadialMenu::Frame{ .Scale = 1.0f, .Min = { 0.0f, 0.0f }, .Max = { 1600.0f, 900.0f } }; };
        const auto onScene = [](ImVec2) { return true; };
        ToolMenu = std::make_unique<ToolRegistryMenuModel>(*Workspace.Interaction.Tools);
        ToolWheel = std::make_unique<RadialMenuSession>(*ToolMenu, KeyChord{ .Key = SDLK_Q, .Mods = {} }, frame, onScene);
        GizmoMenu = std::make_unique<TransformModeMenuModel>([this] { return Workspace.Interaction.Manipulators; });
        GizmoWheel = std::make_unique<RadialMenuSession>(*GizmoMenu, KeyChord{ .Key = SDLK_Z, .Mods = {} }, frame, onScene);
        Router.AddHandler([this](const InputEvent& e, PointerCapture& cap) { return ToolWheel->OnInput(e, cap); });
        Router.AddHandler([this](const InputEvent& e, PointerCapture& cap) { return GizmoWheel->OnInput(e, cap); });
        Router.AddHandler([this](const InputEvent& e, PointerCapture&)
        {
            Reached.push_back(e);
            return InputConsumed::No;
        });
    }
    InputConsumed Press(SDL_Keycode key, ModifierFlags mods = {})
    {
        return Router.Route(KeyDownEvent{ .Key = key, .Modifiers = mods, .Pointer = { 800.0f, 450.0f } });
    }
    InputConsumed Release(SDL_Keycode key)
    {
        return Router.Route(KeyUpEvent{ .Key = key, .Modifiers = {}, .Pointer = { 800.0f, 450.0f } });
    }
    void MoveToSector(const RadialMenuSession& wheel, int index)
    {
        (void)Router.Route(PointerMoveEvent{ .Position = RadialMenu::SlotCenter(wheel.GetLayout(), index), .Delta = {}, .Modifiers = {} });
    }

    InputRouter Router;
    std::unique_ptr<ToolRegistryMenuModel> ToolMenu;
    std::unique_ptr<RadialMenuSession> ToolWheel;
    std::unique_ptr<TransformModeMenuModel> GizmoMenu;
    std::unique_ptr<RadialMenuSession> GizmoWheel;
    std::vector<InputEvent> Reached;
};
}

TEST_F(RadialMenuRoutingTest, AnOpenToolWheelSwallowsTheGizmoWheelsKey)
{
    (void)Press(SDLK_Q);
    ASSERT_EQ(ToolWheel->GetPhase(), RadialMenuPhase::Open);
    EXPECT_EQ(Press(SDLK_Z), InputConsumed::Yes);
    EXPECT_EQ(GizmoWheel->GetPhase(), RadialMenuPhase::Closed);
    (void)Release(SDLK_Z);
    EXPECT_EQ(GizmoWheel->GetPhase(), RadialMenuPhase::Closed);
    (void)Release(SDLK_Q);
    EXPECT_EQ(ToolWheel->GetPhase(), RadialMenuPhase::Closed);
}

TEST_F(RadialMenuRoutingTest, TheGizmoWheelSetsTheModeAndLeavesTheToolWheelAlone)
{
    Workspace.Interaction.Manipulators->SetTransformMode(TransformMode::Move);
    ASSERT_EQ(Workspace.Interaction.Tools->GetActiveTool()->GetId(), "select");
    EXPECT_EQ(Press(SDLK_Z), InputConsumed::Yes);
    ASSERT_EQ(GizmoWheel->GetPhase(), RadialMenuPhase::Open);
    EXPECT_EQ(ToolWheel->GetPhase(), RadialMenuPhase::Closed);
    EXPECT_EQ(GizmoWheel->GetLayout().Count, 4);
    MoveToSector(*GizmoWheel, 2);
    EXPECT_EQ(GizmoWheel->GetHot(), 2);
    // Q while the gizmo wheel is open is the gizmo wheel's to swallow.
    EXPECT_EQ(Press(SDLK_Q), InputConsumed::Yes);
    EXPECT_EQ(ToolWheel->GetPhase(), RadialMenuPhase::Closed);
    (void)Release(SDLK_Q);
    (void)Release(SDLK_Z);
    EXPECT_EQ(GizmoWheel->GetPhase(), RadialMenuPhase::Closed);
    EXPECT_EQ(Workspace.Interaction.Manipulators->GetTransformMode(), TransformMode::Rotate);
    EXPECT_EQ(Workspace.Interaction.Tools->GetActiveTool()->GetId(), "select");
}

TEST_F(RadialMenuRoutingTest, UndoAndRedoChordsAreNotTheGizmoWheelsKey)
{
    Reached.clear();
    EXPECT_EQ(Press(SDLK_Z, ModifierFlags{ .Ctrl = true }), InputConsumed::No);
    EXPECT_EQ(Press(SDLK_Z, ModifierFlags{ .Ctrl = true, .Shift = true }), InputConsumed::No);
    EXPECT_EQ(GizmoWheel->GetPhase(), RadialMenuPhase::Closed);
    EXPECT_EQ(Reached.size(), 2u);
}
