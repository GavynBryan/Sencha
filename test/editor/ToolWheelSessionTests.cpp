#include "WorkspaceFixture.h"

#include "input/InputRouter.h"
#include "tools/ITool.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolWheelSession.h"
#include "workspace/WorkspaceInteractionRuntime.h"

#include <SDL3/SDL_keycode.h>

#include <vector>

// The hold-to-choose gesture as a state machine over the live registry: what
// opens the wheel, what it swallows while open, what a release selects, and
// the ways it must refuse -- another gesture holding the pointer, a held key
// after Escape, a release with no motion, a release over the tool already on.
namespace
{
class SpyTool : public ITool
{
public:
    std::string_view GetId() const override { return "spy"; }
    std::string_view GetDisplayName() const override { return "Spy"; }
    void OnActivate(ToolContext&) override { ++Activations; }
    int Activations = 0;
};

class ToolWheelSessionTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        auto spy = std::make_unique<SpyTool>();
        Spy = spy.get();
        Tools().Register(std::move(spy));
        SpyIndex = static_cast<int>(Tools().GetTools().size()) - 1;

        Wheel = std::make_unique<ToolWheelSession>(
            Tools(), ITool::Shortcut{ .Key = SDLK_Q, .Mods = {} },
            [] { return ToolWheel::Frame{ .Scale = 1.0f, .Min = { 0.0f, 0.0f }, .Max = { 1600.0f, 900.0f } }; });

        // A gesture ahead of the wheel that takes the pointer on a press, as
        // fly-look does; a recorder behind it that sees whatever the wheel let
        // through.
        Router.AddHandler([this](const InputEvent& e, PointerCapture& cap)
        {
            if (std::get_if<PointerDownEvent>(&e) && AheadGrabs)
            {
                cap.Acquire(PointerCaptureKind::Exclusive);
                return InputConsumed::Yes;
            }
            if (std::get_if<PointerUpEvent>(&e) && cap.HeldBySelf())
            {
                cap.Release();
                return InputConsumed::Yes;
            }
            return InputConsumed::No;
        });
        Router.AddHandler([this](const InputEvent& e, PointerCapture& cap) { return Wheel->OnInput(e, cap); });
        Router.AddHandler([this](const InputEvent& e, PointerCapture&)
        {
            Reached.push_back(e);
            return InputConsumed::No;
        });
    }

    [[nodiscard]] ToolRegistry& Tools() { return *Workspace.Interaction.Tools; }

    InputConsumed Press(SDL_Keycode key, ImVec2 at = { 800.0f, 450.0f }, ModifierFlags mods = {})
    {
        return Router.Route(KeyDownEvent{ .Key = key, .Modifiers = mods, .Pointer = at });
    }
    InputConsumed Release(SDL_Keycode key, ModifierFlags mods = {})
    {
        return Router.Route(KeyUpEvent{ .Key = key, .Modifiers = mods, .Pointer = { 800.0f, 450.0f } });
    }
    InputConsumed Move(ImVec2 to)
    {
        return Router.Route(PointerMoveEvent{ .Position = to, .Delta = {}, .Modifiers = {} });
    }
    // A point inside sector `index` of the open wheel.
    [[nodiscard]] ImVec2 InSector(int index) const
    {
        const ToolWheel::Layout& layout = Wheel->GetLayout();
        const ImVec2 slot = ToolWheel::SlotCenter(layout, index);
        // Twice the radius out along the same direction: the wedge, not the button.
        return { layout.Center.x + (slot.x - layout.Center.x) * 2.0f, layout.Center.y + (slot.y - layout.Center.y) * 2.0f };
    }
    [[nodiscard]] std::size_t ReachedMoves() const
    {
        std::size_t n = 0;
        for (const InputEvent& e : Reached)
            n += std::holds_alternative<PointerMoveEvent>(e) ? 1 : 0;
        return n;
    }

    InputRouter Router;
    std::unique_ptr<ToolWheelSession> Wheel;
    SpyTool* Spy = nullptr;
    int SpyIndex = -1;
    bool AheadGrabs = false;
    std::vector<InputEvent> Reached;
};
}

TEST_F(ToolWheelSessionTest, HoldMoveReleaseActivatesTheSectorsTool)
{
    ASSERT_EQ(Tools().GetActiveTool()->GetId(), "select");
    EXPECT_EQ(Press(SDLK_Q, { 300.0f, 300.0f }), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Open);
    EXPECT_FLOAT_EQ(Wheel->GetLayout().Center.x, 300.0f);
    EXPECT_FLOAT_EQ(Wheel->GetLayout().Center.y, 300.0f);
    EXPECT_EQ(Wheel->GetHot(), -1);
    EXPECT_TRUE(Router.PointerCaptured());

    // The pointer is the wheel's while it is open: nothing behind it sees motion.
    EXPECT_EQ(Move(InSector(SpyIndex)), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetHot(), SpyIndex);
    EXPECT_EQ(ReachedMoves(), 0u);

    EXPECT_EQ(Release(SDLK_Q), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Closed);
    EXPECT_FALSE(Router.PointerCaptured());
    EXPECT_EQ(Tools().GetActiveIndex(), SpyIndex);
    EXPECT_EQ(Spy->Activations, 1);
    EXPECT_EQ(Wheel->GetHot(), -1);

    // The pointer is free again.
    (void)Move({ 10.0f, 10.0f });
    EXPECT_EQ(ReachedMoves(), 1u);
}

TEST_F(ToolWheelSessionTest, ReleaseInTheHubSelectsNothing)
{
    (void)Press(SDLK_Q);
    (void)Move(InSector(SpyIndex));
    (void)Move(Wheel->GetLayout().Center);
    EXPECT_EQ(Wheel->GetHot(), -1);
    (void)Release(SDLK_Q);
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Closed);
    EXPECT_EQ(Tools().GetActiveTool()->GetId(), "select");
    EXPECT_EQ(Spy->Activations, 0);
}

TEST_F(ToolWheelSessionTest, AnEdgeOpenWithNoMotionSelectsNothing)
{
    // The wheel is shifted in from the edge, so the pointer already sits in a
    // sector; only motion makes a sector hot.
    (void)Press(SDLK_Q, { 5.0f, 450.0f });
    EXPECT_GT(Wheel->GetLayout().Center.x, 5.0f);
    EXPECT_GE(ToolWheel::SectorAt(Wheel->GetLayout(), { 5.0f, 450.0f }), 0);
    EXPECT_EQ(Wheel->GetHot(), -1);
    (void)Release(SDLK_Q);
    EXPECT_EQ(Tools().GetActiveTool()->GetId(), "select");
    EXPECT_EQ(Spy->Activations, 0);
}

TEST_F(ToolWheelSessionTest, ReleaseOverTheActiveToolDoesNotReactivateIt)
{
    ASSERT_TRUE(Tools().Activate("spy"));
    ASSERT_EQ(Spy->Activations, 1);
    (void)Press(SDLK_Q);
    (void)Move(InSector(SpyIndex));
    (void)Release(SDLK_Q);
    EXPECT_EQ(Tools().GetActiveIndex(), SpyIndex);
    EXPECT_EQ(Spy->Activations, 1);
}

TEST_F(ToolWheelSessionTest, EscapeDismissesAndAHeldKeyCannotReopenUntilReleased)
{
    (void)Press(SDLK_Q);
    (void)Move(InSector(SpyIndex));
    EXPECT_EQ(Press(SDLK_ESCAPE), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Dismissed);
    EXPECT_FALSE(Router.PointerCaptured());
    EXPECT_EQ(Spy->Activations, 0);

    // Q down again while it was never released: not a fresh gesture.
    EXPECT_EQ(Press(SDLK_Q), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Dismissed);
    // The editor works normally under the held, dismissed key.
    Reached.clear();
    EXPECT_EQ(Press(SDLK_V), InputConsumed::No);
    EXPECT_EQ(Reached.size(), 1u);
    (void)Move({ 10.0f, 10.0f });
    EXPECT_EQ(ReachedMoves(), 1u);

    EXPECT_EQ(Release(SDLK_Q), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Closed);
    EXPECT_EQ(Spy->Activations, 0);
    EXPECT_EQ(Press(SDLK_Q), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Open);
}

TEST_F(ToolWheelSessionTest, FocusLossClosesWithoutSelecting)
{
    (void)Press(SDLK_Q);
    (void)Move(InSector(SpyIndex));
    EXPECT_EQ(Router.Route(FocusLostEvent{}), InputConsumed::No);
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Closed);
    EXPECT_FALSE(Router.PointerCaptured());
    EXPECT_EQ(Spy->Activations, 0);
    // The release that follows, with nothing open, is nobody's.
    EXPECT_EQ(Release(SDLK_Q), InputConsumed::No);
}

TEST_F(ToolWheelSessionTest, AChordWithModifiersIsNotTheWheelsKey)
{
    EXPECT_EQ(Press(SDLK_Q, { 800.0f, 450.0f }, ModifierFlags{ .Shift = true }), InputConsumed::No);
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Closed);
    EXPECT_EQ(Press(SDLK_W), InputConsumed::No);
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Closed);
}

TEST_F(ToolWheelSessionTest, TheKeyYieldsToAGestureAlreadyHoldingThePointer)
{
    AheadGrabs = true;
    (void)Router.Route(PointerDownEvent{ .Position = { 1.0f, 1.0f }, .Button = MouseButton::Right, .Modifiers = {} });
    ASSERT_TRUE(Router.PointerCaptured());
    Reached.clear();
    EXPECT_EQ(Press(SDLK_Q), InputConsumed::No);
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Closed);
    EXPECT_EQ(Reached.size(), 1u); // the key went on to the handlers behind
    (void)Router.Route(PointerUpEvent{ .Position = { 1.0f, 1.0f }, .Button = MouseButton::Right, .Modifiers = {} });
    EXPECT_FALSE(Router.PointerCaptured());
    EXPECT_EQ(Release(SDLK_Q), InputConsumed::No);
}

TEST_F(ToolWheelSessionTest, ClicksAndKeysAreSwallowedWhileOpen)
{
    (void)Press(SDLK_Q);
    (void)Move(InSector(SpyIndex));
    Reached.clear();
    EXPECT_EQ(Router.Route(PointerDownEvent{ .Position = InSector(SpyIndex), .Button = MouseButton::Left, .Modifiers = {} }),
              InputConsumed::Yes);
    EXPECT_EQ(Router.Route(PointerUpEvent{ .Position = InSector(SpyIndex), .Button = MouseButton::Left, .Modifiers = {} }),
              InputConsumed::Yes);
    EXPECT_EQ(Router.Route(WheelEvent{ .Position = {}, .Delta = 1.0f, .Modifiers = {} }), InputConsumed::Yes);
    EXPECT_EQ(Press(SDLK_V), InputConsumed::Yes);
    EXPECT_TRUE(Reached.empty());
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Open);
    EXPECT_EQ(Spy->Activations, 0);
    EXPECT_EQ(Tools().GetActiveTool()->GetId(), "select");
    // The release still selects: a click changed nothing.
    (void)Release(SDLK_Q, ModifierFlags{ .Shift = true });
    EXPECT_EQ(Tools().GetActiveIndex(), SpyIndex);
}
