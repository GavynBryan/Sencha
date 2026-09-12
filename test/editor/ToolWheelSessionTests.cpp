#include "WorkspaceFixture.h"

#include "brush/BrushMesh.h"
#include "commands/CommandStack.h"
#include "document/EditorScene.h"
#include "document/tools/BrushTool.h"
#include "selection/SelectionService.h"
#include "tools/ToolContext.h"
#include "input/InputRouter.h"
#include "tools/ITool.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolWheelSession.h"
#include "workspace/WorkspaceInteractionRuntime.h"

#include <SDL3/SDL_keycode.h>

#include <span>
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
    void CommitPending(ToolContext&) override { Committed.push_back(VariantSelections); }
    // Variants only when a test gives it some; nothing about the wheel's
    // primary gesture depends on a tool having them.
    std::span<const Variant> GetVariants() const override { return Variants; }
    int GetActiveVariant(const ToolContext&) const override { return Current; }
    void SelectVariant(ToolContext&, std::size_t index) override
    {
        Current = static_cast<int>(index);
        ++VariantSelections;
    }
    int Activations = 0;
    int VariantSelections = 0;
    int Current = -1;
    std::vector<Variant> Variants;
    // VariantSelections as of each CommitPending, so a test can see it came first.
    std::vector<int> Committed;
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
            [] { return ToolWheel::Frame{ .Scale = 1.0f, .Min = { 0.0f, 0.0f }, .Max = { 1600.0f, 900.0f } }; },
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

    [[nodiscard]] ToolRegistry& Tools() const { return *Workspace.Interaction.Tools; }

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
    // The centre of variant `index` of tool `parent`'s fan on the open wheel.
    [[nodiscard]] ImVec2 InVariant(int parent, int index) const
    {
        const int count = static_cast<int>(Tools().GetTools()[static_cast<std::size_t>(parent)]->GetVariants().size());
        return ToolWheel::VariantSlotCenter(Wheel->GetLayout(), parent, index, count);
    }
    [[nodiscard]] int IndexOf(std::string_view id) const
    {
        const auto& tools = Tools().GetTools();
        for (std::size_t i = 0; i < tools.size(); ++i)
            if (tools[i]->GetId() == id)
                return static_cast<int>(i);
        return -1;
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
    // What the application answers when asked whether the pointer is on a
    // scene viewport, and every position it was asked about.
    bool OnScene = true;
    std::vector<ImVec2> Asked;
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

TEST_F(ToolWheelSessionTest, OffTheSceneTheKeyIsSwallowedAndNothingOpens)
{
    OnScene = false;
    Reached.clear();
    EXPECT_EQ(Press(SDLK_Q, { 40.0f, 40.0f }), InputConsumed::Yes);
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Closed);
    EXPECT_FALSE(Router.PointerCaptured());
    EXPECT_TRUE(Reached.empty());
    // The question was asked about where the key went down.
    ASSERT_EQ(Asked.size(), 1u);
    EXPECT_FLOAT_EQ(Asked.front().x, 40.0f);
    EXPECT_FLOAT_EQ(Asked.front().y, 40.0f);
    // Motion and the release are nobody's: the editor is untouched.
    (void)Move({ 500.0f, 500.0f });
    EXPECT_EQ(ReachedMoves(), 1u);
    EXPECT_EQ(Release(SDLK_Q), InputConsumed::No);
    EXPECT_EQ(Spy->Activations, 0);
    EXPECT_EQ(Tools().GetActiveTool()->GetId(), "select");
}

TEST_F(ToolWheelSessionTest, TheAnswerIsAskedOnlyAtThePressAndNeverWhileOpenOrDismissed)
{
    (void)Press(SDLK_Q);
    ASSERT_EQ(Asked.size(), 1u);
    OnScene = false;
    (void)Move(InSector(SpyIndex));
    (void)Press(SDLK_ESCAPE);
    (void)Press(SDLK_Q);
    (void)Release(SDLK_Q);
    EXPECT_EQ(Asked.size(), 1u);
    EXPECT_EQ(Wheel->GetPhase(), ToolWheelPhase::Closed);
}

TEST_F(ToolWheelSessionTest, AHeldGestureStillOwnsTheKeyBeforeTheSceneIsAsked)
{
    AheadGrabs = true;
    OnScene = false;
    (void)Router.Route(PointerDownEvent{ .Position = { 1.0f, 1.0f }, .Button = MouseButton::Right, .Modifiers = {} });
    EXPECT_EQ(Press(SDLK_Q), InputConsumed::No);
    EXPECT_TRUE(Asked.empty());
}

// The variant ring. The select tool has four (the element kinds) and the
// brush three (its primitives); the spy gets two when a test says so.
TEST_F(ToolWheelSessionTest, MovingOutFromAToolIntoItsVariantsKeepsTheTool)
{
    const int select = IndexOf("select");
    ASSERT_EQ(Tools().GetTools()[static_cast<std::size_t>(select)]->GetVariants().size(), 4u);
    (void)Press(SDLK_Q);
    EXPECT_EQ(Wheel->GetLayout().MaxVariants, 4);
    (void)Move(ToolWheel::SlotCenter(Wheel->GetLayout(), select));
    EXPECT_EQ(Wheel->GetHot(), select);
    EXPECT_EQ(Wheel->GetHotVariant(), -1);
    for (int i = 0; i < 4; ++i)
    {
        // Diagonally: halfway from the tool's slot to the child is still the
        // tool, and the child itself is the child.
        const ImVec2 slot = ToolWheel::SlotCenter(Wheel->GetLayout(), select);
        const ImVec2 child = InVariant(select, i);
        (void)Move({ (slot.x + child.x) * 0.5f, (slot.y + child.y) * 0.5f });
        EXPECT_EQ(Wheel->GetHot(), select) << "variant " << i;
        (void)Move(child);
        EXPECT_EQ(Wheel->GetHot(), select) << "variant " << i;
        EXPECT_EQ(Wheel->GetHotVariant(), i);
    }
    // Back inside the rim, direction picks again and no variant is hot.
    (void)Move(ToolWheel::SlotCenter(Wheel->GetLayout(), IndexOf("brush")));
    EXPECT_EQ(Wheel->GetHot(), IndexOf("brush"));
    EXPECT_EQ(Wheel->GetHotVariant(), -1);
    (void)Release(SDLK_Q);
}

TEST_F(ToolWheelSessionTest, SweepingTheOuterRingPastAFansEdgeHandsOverToTheNeighbour)
{
    const int select = IndexOf("select");
    const int brush = IndexOf("brush");
    (void)Press(SDLK_Q);
    (void)Move(ToolWheel::SlotCenter(Wheel->GetLayout(), select));
    (void)Move(InVariant(select, 3));
    // The last child lies on the select fan's flank, past the select
    // sector's own edge: the tool holds it because the pointer is in its fan.
    EXPECT_EQ(ToolWheel::SectorAt(Wheel->GetLayout(), InVariant(select, 3)), brush);
    EXPECT_EQ(Wheel->GetHot(), select);
    EXPECT_EQ(Wheel->GetHotVariant(), 3);
    // Around the outer band, without coming back in: the brush's middle child
    // is past the select fan's edge, so the brush takes over; its first child
    // meets the select fan's edge, and now that the brush holds the pointer
    // it is the brush's.
    (void)Move(InVariant(brush, 1));
    EXPECT_EQ(Wheel->GetHot(), brush);
    EXPECT_EQ(Wheel->GetHotVariant(), 1);
    (void)Move(InVariant(brush, 0));
    EXPECT_EQ(Wheel->GetHot(), brush);
    EXPECT_EQ(Wheel->GetHotVariant(), 0);
    (void)Move(InVariant(brush, 2));
    EXPECT_EQ(Wheel->GetHot(), brush);
    EXPECT_EQ(Wheel->GetHotVariant(), 2);
    (void)Release(SDLK_Q);
}

TEST_F(ToolWheelSessionTest, ReleaseOnAVariantActivatesTheToolThenSelectsTheVariant)
{
    const int select = IndexOf("select");
    const int brush = IndexOf("brush");
    ASSERT_TRUE(Tools().Activate("spy"));
    ASSERT_EQ(Workspace.MeshEdit.GetElementKind(), MeshElementKind::Object);

    (void)Press(SDLK_Q);
    (void)Move(ToolWheel::SlotCenter(Wheel->GetLayout(), select));
    (void)Move(InVariant(select, 3));
    (void)Release(SDLK_Q);
    EXPECT_EQ(Tools().GetActiveIndex(), select);
    EXPECT_EQ(Workspace.MeshEdit.GetElementKind(), MeshElementKind::Face);

    (void)Press(SDLK_Q);
    (void)Move(ToolWheel::SlotCenter(Wheel->GetLayout(), brush));
    (void)Move(InVariant(brush, 2));
    (void)Release(SDLK_Q);
    EXPECT_EQ(Tools().GetActiveIndex(), brush);
    auto& brushTool = static_cast<BrushTool&>(*Tools().GetTools()[static_cast<std::size_t>(brush)]);
    EXPECT_EQ(brushTool.Creation.ActivePrimitive, BrushPrimitive::Cylinder);
    // The brush's entry dropped the element mode, and the variant came after.
    EXPECT_EQ(Workspace.MeshEdit.GetElementKind(), MeshElementKind::Object);

    // The tool alone, from the primary ring: what it has stays.
    ASSERT_TRUE(Tools().Activate("select"));
    (void)Press(SDLK_Q);
    (void)Move(ToolWheel::SlotCenter(Wheel->GetLayout(), brush));
    EXPECT_EQ(Wheel->GetHotVariant(), -1);
    (void)Release(SDLK_Q);
    EXPECT_EQ(Tools().GetActiveIndex(), brush);
    EXPECT_EQ(brushTool.Creation.ActivePrimitive, BrushPrimitive::Cylinder);
}

TEST_F(ToolWheelSessionTest, ReleaseOnAVariantOfTheActiveToolSelectsItWithoutReentry)
{
    Spy->Variants = { { .Label = "A" }, { .Label = "B" } };
    ASSERT_TRUE(Tools().Activate("spy"));
    ASSERT_EQ(Spy->Activations, 1);
    (void)Press(SDLK_Q);
    (void)Move(InVariant(SpyIndex, 1));
    EXPECT_EQ(Wheel->GetHot(), SpyIndex);
    EXPECT_EQ(Wheel->GetHotVariant(), 1);
    (void)Release(SDLK_Q);
    EXPECT_EQ(Spy->Activations, 1);
    EXPECT_EQ(Spy->VariantSelections, 1);
    EXPECT_EQ(Spy->Current, 1);
    // Whatever the tool had staged was placed before the variant changed.
    ASSERT_EQ(Spy->Committed.size(), 1u);
    EXPECT_EQ(Spy->Committed.front(), 0);
}

TEST_F(ToolWheelSessionTest, AVariantChosenOverAPendingBrushPlacesItInsteadOfReshapingIt)
{
    const int brush = IndexOf("brush");
    ASSERT_TRUE(Tools().Activate("brush"));
    auto& brushTool = static_cast<BrushTool&>(*Tools().GetTools()[static_cast<std::size_t>(brush)]);
    ToolContext& ctx = Tools().GetContext();
    brushTool.SetPending(ctx, BrushTool::PendingBrush{ .Center = { 0.0, 0.5, 0.0 }, .HalfExtents = { 0.5, 0.5, 0.5 },
                                                       .DepthSign = 1.0f, .DragDepthHalf = 0.5f });
    ASSERT_TRUE(brushTool.HasPending());
    ASSERT_EQ(ctx.Selection.GetSelection().size(), 1u);
    const EntityId placed = ctx.Selection.GetSelection().front().Entity;
    const BrushMesh* before = ctx.Scene.TryGetBrushMesh(placed);
    ASSERT_NE(before, nullptr);
    const std::size_t boxFaces = before->Faces.size();

    (void)Press(SDLK_Q);
    (void)Move(ToolWheel::SlotCenter(Wheel->GetLayout(), brush));
    (void)Move(InVariant(brush, 2));
    (void)Release(SDLK_Q);

    // The box is real and still a box; cylinders are what comes next.
    EXPECT_FALSE(brushTool.HasPending());
    EXPECT_EQ(brushTool.Creation.ActivePrimitive, BrushPrimitive::Cylinder);
    const BrushMesh* after = ctx.Scene.TryGetBrushMesh(placed);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->Faces.size(), boxFaces);
    EXPECT_TRUE(Workspace.Commands.CanUndo());
}

TEST_F(ToolWheelSessionTest, AToolWithoutVariantsKeepsItsWholeWedge)
{
    const int cut = IndexOf("edgecut");
    ASSERT_TRUE(Tools().GetTools()[static_cast<std::size_t>(cut)]->GetVariants().empty());
    (void)Press(SDLK_Q);
    (void)Move(InSector(cut)); // twice the radius out: the outer band
    EXPECT_EQ(ToolWheel::RingAt(Wheel->GetLayout(), InSector(cut)), ToolWheel::Ring::Outer);
    EXPECT_EQ(Wheel->GetHot(), cut);
    EXPECT_EQ(Wheel->GetHotVariant(), -1);
    (void)Release(SDLK_Q);
    EXPECT_EQ(Tools().GetActiveIndex(), cut);
    EXPECT_EQ(Spy->VariantSelections, 0);
}

TEST_F(ToolWheelSessionTest, EscapeAndFocusLossDropTheHotVariantWithoutSelecting)
{
    const int select = IndexOf("select");
    (void)Press(SDLK_Q);
    (void)Move(ToolWheel::SlotCenter(Wheel->GetLayout(), select));
    (void)Move(InVariant(select, 3));
    ASSERT_EQ(Wheel->GetHotVariant(), 3);
    (void)Press(SDLK_ESCAPE);
    EXPECT_EQ(Wheel->GetHotVariant(), -1);
    (void)Release(SDLK_Q);
    EXPECT_EQ(Workspace.MeshEdit.GetElementKind(), MeshElementKind::Object);

    (void)Press(SDLK_Q);
    (void)Move(ToolWheel::SlotCenter(Wheel->GetLayout(), select));
    (void)Move(InVariant(select, 2));
    ASSERT_EQ(Wheel->GetHotVariant(), 2);
    (void)Router.Route(FocusLostEvent{});
    EXPECT_EQ(Wheel->GetHotVariant(), -1);
    EXPECT_EQ(Workspace.MeshEdit.GetElementKind(), MeshElementKind::Object);
}

TEST_F(ToolWheelSessionTest, WithNoToolHotTheOuterBandGoesByDirection)
{
    const int select = IndexOf("select");
    const int brush = IndexOf("brush");
    // Six tools make 60 degree sectors and the select fan of four is wider,
    // so its last child sits in the brush's direction. Reached with no fan
    // showing, that is the brush; reached from the select petal, it is select's.
    (void)Press(SDLK_Q);
    ASSERT_EQ(ToolWheel::SectorAt(Wheel->GetLayout(), InVariant(select, 3)), brush);
    (void)Move(InVariant(select, 3));
    EXPECT_EQ(Wheel->GetHot(), brush);
    (void)Move(ToolWheel::SlotCenter(Wheel->GetLayout(), select));
    (void)Move(InVariant(select, 3));
    EXPECT_EQ(Wheel->GetHot(), select);
    EXPECT_EQ(Wheel->GetHotVariant(), 3);
    (void)Release(SDLK_Q);
}
