#include "input/InputRouter.h"
#include "tools/RadialMenuModel.h"
#include "tools/RadialMenuSession.h"

#include <SDL3/SDL_keycode.h>

#include <gtest/gtest.h>

#include <utility>
#include <vector>

// The hold-to-choose gesture as a state machine over a model it knows nothing
// about: what opens the wheel, what it swallows while open, what a release
// selects, the ways it must refuse -- another gesture holding the pointer, a
// held key after Escape, a release with no motion -- and how the pointer
// travels between an entry and its variants. No tool, registry or workspace:
// the model here is a fake that records what the session asked of it.
namespace
{
class FakeRadialMenuModel : public IRadialMenuModel
{
public:
    std::vector<MenuItem> Items;
    std::vector<std::vector<MenuItem>> VariantsOf; // per item; sized with Items
    int Active = -1;
    int ActiveVariantOf = -1;
    std::vector<std::pair<int, int>> Selected;

    void Configure(int count, std::vector<int> variantCounts)
    {
        Items.assign(static_cast<std::size_t>(count), MenuItem{ .Label = "E" });
        VariantsOf.assign(static_cast<std::size_t>(count), {});
        for (std::size_t i = 0; i < variantCounts.size() && i < VariantsOf.size(); ++i)
            VariantsOf[i].assign(static_cast<std::size_t>(variantCounts[i]), MenuItem{ .Label = "V" });
    }

    int Count() const override { return static_cast<int>(Items.size()); }
    MenuItem Item(int index) const override { return Items[static_cast<std::size_t>(index)]; }
    int ActiveIndex() const override { return Active; }
    std::span<const MenuItem> Variants(int index) const override { return VariantsOf[static_cast<std::size_t>(index)]; }
    int ActiveVariant(int) const override { return ActiveVariantOf; }
    void Select(int index, int variant) override { Selected.emplace_back(index, variant); }
};

class RadialMenuSessionTest : public testing::Test
{
protected:
    void SetUp() override
    {
        // Six entries; the first has four variants, the second three, the
        // rest none, so sectors are 60 degrees and the first fan is wider.
        Menu.Configure(6, { 4, 3 });
        Menu.Active = 0;
        Wheel = std::make_unique<RadialMenuSession>(
            Menu, KeyChord{ .Key = SDLK_Q, .Mods = {} },
            [] { return RadialMenu::Frame{ .Scale = 1.0f, .Min = { 0.0f, 0.0f }, .Max = { 1600.0f, 900.0f } }; },
            [this](ImVec2 pointer)
            {
                Asked.push_back(pointer);
                return OnScene;
            });

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
    // A point inside sector `index` of the open wheel: twice the radius out,
    // the wedge rather than the button.
    [[nodiscard]] ImVec2 InSector(int index) const
    {
        const RadialMenu::Layout& layout = Wheel->GetLayout();
        const ImVec2 slot = RadialMenu::SlotCenter(layout, index);
        return { layout.Center.x + (slot.x - layout.Center.x) * 2.0f, layout.Center.y + (slot.y - layout.Center.y) * 2.0f };
    }
    [[nodiscard]] ImVec2 OnSlot(int index) const { return RadialMenu::SlotCenter(Wheel->GetLayout(), index); }
    [[nodiscard]] ImVec2 InVariant(int parent, int index) const
    {
        return RadialMenu::VariantSlotCenter(Wheel->GetLayout(), parent, index, static_cast<int>(Menu.VariantsOf[static_cast<std::size_t>(parent)].size()));
    }
    [[nodiscard]] std::size_t ReachedMoves() const
    {
        std::size_t n = 0;
        for (const InputEvent& e : Reached)
            n += std::holds_alternative<PointerMoveEvent>(e) ? 1 : 0;
        return n;
    }

    FakeRadialMenuModel Menu;
    InputRouter Router;
    std::unique_ptr<RadialMenuSession> Wheel;
    bool AheadGrabs = false;
    bool OnScene = true;
    std::vector<ImVec2> Asked;
    std::vector<InputEvent> Reached;
};
}

TEST_F(RadialMenuSessionTest, HoldMoveReleaseSelectsTheSectorsEntry)
{
    EXPECT_EQ(Press(SDLK_Q, { 300.0f, 300.0f }), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Open);
    EXPECT_FLOAT_EQ(Wheel->GetLayout().Center.x, 300.0f);
    EXPECT_FLOAT_EQ(Wheel->GetLayout().Center.y, 300.0f);
    EXPECT_EQ(Wheel->GetLayout().Count, 6);
    EXPECT_EQ(Wheel->GetLayout().MaxVariants, 4);
    EXPECT_EQ(Wheel->GetHot(), -1);
    EXPECT_TRUE(Router.PointerCaptured());

    // The pointer is the wheel's while it is open: nothing behind it sees motion.
    EXPECT_EQ(Move(InSector(5)), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetHot(), 5);
    EXPECT_EQ(ReachedMoves(), 0u);

    EXPECT_EQ(Release(SDLK_Q), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Closed);
    EXPECT_FALSE(Router.PointerCaptured());
    ASSERT_EQ(Menu.Selected.size(), 1u);
    EXPECT_EQ(Menu.Selected.front(), std::make_pair(5, -1));
    EXPECT_EQ(Wheel->GetHot(), -1);

    // The pointer is free again.
    (void)Move({ 10.0f, 10.0f });
    EXPECT_EQ(ReachedMoves(), 1u);
}

TEST_F(RadialMenuSessionTest, ReleaseInTheHubSelectsNothing)
{
    (void)Press(SDLK_Q);
    (void)Move(InSector(5));
    (void)Move(Wheel->GetLayout().Center);
    EXPECT_EQ(Wheel->GetHot(), -1);
    (void)Release(SDLK_Q);
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Closed);
    EXPECT_TRUE(Menu.Selected.empty());
}

TEST_F(RadialMenuSessionTest, AnEdgeOpenWithNoMotionSelectsNothing)
{
    // The wheel is shifted in from the edge, so the pointer already sits in a
    // sector; only motion makes a sector hot.
    (void)Press(SDLK_Q, { 5.0f, 450.0f });
    EXPECT_GT(Wheel->GetLayout().Center.x, 5.0f);
    EXPECT_GE(RadialMenu::SectorAt(Wheel->GetLayout(), { 5.0f, 450.0f }), 0);
    EXPECT_EQ(Wheel->GetHot(), -1);
    (void)Release(SDLK_Q);
    EXPECT_TRUE(Menu.Selected.empty());
}

TEST_F(RadialMenuSessionTest, ReleaseOverTheActiveEntryIsStillTheModelsCall)
{
    // The session reports the choice; what a release over the active entry
    // means (re-enter, ignore) is the model's rule.
    Menu.Active = 5;
    (void)Press(SDLK_Q);
    (void)Move(InSector(5));
    (void)Release(SDLK_Q);
    ASSERT_EQ(Menu.Selected.size(), 1u);
    EXPECT_EQ(Menu.Selected.front(), std::make_pair(5, -1));
}

TEST_F(RadialMenuSessionTest, EscapeDismissesAndAHeldKeyCannotReopenUntilReleased)
{
    (void)Press(SDLK_Q);
    (void)Move(InSector(5));
    EXPECT_EQ(Press(SDLK_ESCAPE), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Dismissed);
    EXPECT_FALSE(Router.PointerCaptured());
    EXPECT_TRUE(Menu.Selected.empty());

    // Q down again while it was never released: not a fresh gesture.
    EXPECT_EQ(Press(SDLK_Q), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Dismissed);
    // The editor works normally under the held, dismissed key.
    Reached.clear();
    EXPECT_EQ(Press(SDLK_V), InputConsumed::No);
    EXPECT_EQ(Reached.size(), 1u);
    (void)Move({ 10.0f, 10.0f });
    EXPECT_EQ(ReachedMoves(), 1u);

    EXPECT_EQ(Release(SDLK_Q), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Closed);
    EXPECT_TRUE(Menu.Selected.empty());
    EXPECT_EQ(Press(SDLK_Q), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Open);
}

TEST_F(RadialMenuSessionTest, FocusLossClosesWithoutSelecting)
{
    (void)Press(SDLK_Q);
    (void)Move(InSector(5));
    EXPECT_EQ(Router.Route(FocusLostEvent{}), InputConsumed::No);
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Closed);
    EXPECT_FALSE(Router.PointerCaptured());
    EXPECT_TRUE(Menu.Selected.empty());
    // The release that follows, with nothing open, is nobody's.
    EXPECT_EQ(Release(SDLK_Q), InputConsumed::No);
}

TEST_F(RadialMenuSessionTest, AChordWithModifiersIsNotTheWheelsKey)
{
    EXPECT_EQ(Press(SDLK_Q, { 800.0f, 450.0f }, ModifierFlags{ .Shift = true }), InputConsumed::No);
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Closed);
    EXPECT_EQ(Press(SDLK_W), InputConsumed::No);
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Closed);
}

TEST_F(RadialMenuSessionTest, TheKeyYieldsToAGestureAlreadyHoldingThePointer)
{
    AheadGrabs = true;
    (void)Router.Route(PointerDownEvent{ .Position = { 1.0f, 1.0f }, .Button = MouseButton::Right, .Modifiers = {} });
    ASSERT_TRUE(Router.PointerCaptured());
    Reached.clear();
    EXPECT_EQ(Press(SDLK_Q), InputConsumed::No);
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Closed);
    EXPECT_EQ(Reached.size(), 1u); // the key went on to the handlers behind
    EXPECT_TRUE(Asked.empty());    // and the scene was never asked
    (void)Router.Route(PointerUpEvent{ .Position = { 1.0f, 1.0f }, .Button = MouseButton::Right, .Modifiers = {} });
    EXPECT_FALSE(Router.PointerCaptured());
    EXPECT_EQ(Release(SDLK_Q), InputConsumed::No);
}

TEST_F(RadialMenuSessionTest, ClicksAndKeysAreSwallowedWhileOpen)
{
    (void)Press(SDLK_Q);
    (void)Move(InSector(5));
    Reached.clear();
    EXPECT_EQ(Router.Route(PointerDownEvent{ .Position = InSector(5), .Button = MouseButton::Left, .Modifiers = {} }),
              InputConsumed::Yes);
    EXPECT_EQ(Router.Route(PointerUpEvent{ .Position = InSector(5), .Button = MouseButton::Left, .Modifiers = {} }),
              InputConsumed::Yes);
    EXPECT_EQ(Router.Route(WheelEvent{ .Position = {}, .Delta = 1.0f, .Modifiers = {} }), InputConsumed::Yes);
    EXPECT_EQ(Press(SDLK_V), InputConsumed::Yes);
    EXPECT_TRUE(Reached.empty());
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Open);
    EXPECT_TRUE(Menu.Selected.empty());
    // The release still selects, whatever the hand holds by then.
    (void)Release(SDLK_Q, ModifierFlags{ .Shift = true });
    ASSERT_EQ(Menu.Selected.size(), 1u);
    EXPECT_EQ(Menu.Selected.front().first, 5);
}

TEST_F(RadialMenuSessionTest, OffTheSceneTheKeyIsSwallowedAndNothingOpens)
{
    OnScene = false;
    Reached.clear();
    EXPECT_EQ(Press(SDLK_Q, { 40.0f, 40.0f }), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Closed);
    EXPECT_FALSE(Router.PointerCaptured());
    EXPECT_TRUE(Reached.empty());
    // The question was asked about where the key went down.
    ASSERT_EQ(Asked.size(), 1u);
    EXPECT_FLOAT_EQ(Asked.front().x, 40.0f);
    EXPECT_FLOAT_EQ(Asked.front().y, 40.0f);
    // Motion and the release are nobody's.
    (void)Move({ 500.0f, 500.0f });
    EXPECT_EQ(ReachedMoves(), 1u);
    EXPECT_EQ(Release(SDLK_Q), InputConsumed::No);
    EXPECT_TRUE(Menu.Selected.empty());
}

TEST_F(RadialMenuSessionTest, TheAnswerIsAskedOnlyAtThePressAndNeverWhileOpenOrDismissed)
{
    (void)Press(SDLK_Q);
    ASSERT_EQ(Asked.size(), 1u);
    OnScene = false;
    (void)Move(InSector(5));
    (void)Press(SDLK_ESCAPE);
    (void)Press(SDLK_Q);
    (void)Release(SDLK_Q);
    EXPECT_EQ(Asked.size(), 1u);
    EXPECT_EQ(Wheel->GetPhase(), RadialMenuPhase::Closed);
}

// The variant fan: entry 0 has four, entry 1 three, the rest none.
TEST_F(RadialMenuSessionTest, MovingOutFromAnEntryIntoItsVariantsKeepsTheEntry)
{
    (void)Press(SDLK_Q);
    (void)Move(OnSlot(0));
    EXPECT_EQ(Wheel->GetHot(), 0);
    EXPECT_EQ(Wheel->GetHotVariant(), -1);
    for (int i = 0; i < 4; ++i)
    {
        const ImVec2 slot = OnSlot(0);
        const ImVec2 child = InVariant(0, i);
        (void)Move({ (slot.x + child.x) * 0.5f, (slot.y + child.y) * 0.5f });
        EXPECT_EQ(Wheel->GetHot(), 0) << "variant " << i;
        (void)Move(child);
        EXPECT_EQ(Wheel->GetHot(), 0) << "variant " << i;
        EXPECT_EQ(Wheel->GetHotVariant(), i);
    }
    // Back inside the rim, direction picks again and no variant is hot.
    (void)Move(OnSlot(1));
    EXPECT_EQ(Wheel->GetHot(), 1);
    EXPECT_EQ(Wheel->GetHotVariant(), -1);
    (void)Release(SDLK_Q);
}

TEST_F(RadialMenuSessionTest, SweepingTheOuterRingPastAFansEdgeHandsOverToTheNeighbour)
{
    (void)Press(SDLK_Q);
    (void)Move(OnSlot(0));
    (void)Move(InVariant(0, 3));
    // The last child lies on the fan's flank, past the entry's own sector.
    EXPECT_EQ(RadialMenu::SectorAt(Wheel->GetLayout(), InVariant(0, 3)), 1);
    EXPECT_EQ(Wheel->GetHot(), 0);
    EXPECT_EQ(Wheel->GetHotVariant(), 3);
    // Around the outer band: the neighbour's middle child is past this fan's
    // edge, so the neighbour takes over; its first child meets this fan's
    // edge, and now that the neighbour holds the pointer it is the neighbour's.
    (void)Move(InVariant(1, 1));
    EXPECT_EQ(Wheel->GetHot(), 1);
    EXPECT_EQ(Wheel->GetHotVariant(), 1);
    (void)Move(InVariant(1, 0));
    EXPECT_EQ(Wheel->GetHot(), 1);
    EXPECT_EQ(Wheel->GetHotVariant(), 0);
    (void)Release(SDLK_Q);
}

TEST_F(RadialMenuSessionTest, WithNoEntryHotTheOuterBandGoesByDirection)
{
    (void)Press(SDLK_Q);
    ASSERT_EQ(RadialMenu::SectorAt(Wheel->GetLayout(), InVariant(0, 3)), 1);
    (void)Move(InVariant(0, 3));
    EXPECT_EQ(Wheel->GetHot(), 1);
    (void)Move(OnSlot(0));
    (void)Move(InVariant(0, 3));
    EXPECT_EQ(Wheel->GetHot(), 0);
    EXPECT_EQ(Wheel->GetHotVariant(), 3);
    (void)Release(SDLK_Q);
}

TEST_F(RadialMenuSessionTest, ReleaseOnAVariantSelectsTheEntryWithThatVariant)
{
    (void)Press(SDLK_Q);
    (void)Move(OnSlot(0));
    (void)Move(InVariant(0, 3));
    (void)Release(SDLK_Q);
    ASSERT_EQ(Menu.Selected.size(), 1u);
    EXPECT_EQ(Menu.Selected.front(), std::make_pair(0, 3));
}

TEST_F(RadialMenuSessionTest, AnEntryWithoutVariantsKeepsItsWholeWedge)
{
    (void)Press(SDLK_Q);
    (void)Move(InSector(2)); // twice the radius out: the outer band
    EXPECT_EQ(RadialMenu::RingAt(Wheel->GetLayout(), InSector(2)), RadialMenu::Ring::Outer);
    EXPECT_EQ(Wheel->GetHot(), 2);
    EXPECT_EQ(Wheel->GetHotVariant(), -1);
    (void)Release(SDLK_Q);
    ASSERT_EQ(Menu.Selected.size(), 1u);
    EXPECT_EQ(Menu.Selected.front(), std::make_pair(2, -1));
}

TEST_F(RadialMenuSessionTest, EscapeAndFocusLossDropTheHotVariantWithoutSelecting)
{
    (void)Press(SDLK_Q);
    (void)Move(OnSlot(0));
    (void)Move(InVariant(0, 3));
    ASSERT_EQ(Wheel->GetHotVariant(), 3);
    (void)Press(SDLK_ESCAPE);
    EXPECT_EQ(Wheel->GetHotVariant(), -1);
    (void)Release(SDLK_Q);

    (void)Press(SDLK_Q);
    (void)Move(OnSlot(0));
    (void)Move(InVariant(0, 2));
    ASSERT_EQ(Wheel->GetHotVariant(), 2);
    (void)Router.Route(FocusLostEvent{});
    EXPECT_EQ(Wheel->GetHotVariant(), -1);
    EXPECT_TRUE(Menu.Selected.empty());
}
