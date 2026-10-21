#include "WorkspaceFixture.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "document/EntityNameComponent.h"
#include "document/SpaceComponents.h"
#include "document/tools/BrushTool.h"
#include "input/InputRouter.h"
#include "input/ViewportToolDispatcher.h"
#include "operations/TriggerVolumeEdits.h"
#include "tools/ToolRegistry.h"
#include "viewport/EditorViewport.h"
#include "viewport/ViewportProjection.h"
#include "workspace/EntityVerbs.h"

#include <core/json/JsonStringify.h>
#include <logic/TriggerPrograms.h>
#include <physics/components/Collider.h>
#include <world/identity/PersistentEntityIndex.h>

#include <SDL3/SDL_keycode.h>

namespace
{
class TriggerVolumeAuthoringTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        for (const auto& viewport : Workspace.Layout.All())
            viewport->RegionMin = viewport->RegionMax = {};
        auto& top = *Workspace.Layout.All()[0];
        top.ApplyOrientation(ViewportOrientation::Top);
        top.RegionMin = {};
        top.RegionMax = { 400, 400 };
        Top = top.Id;
        Router.AddHandler([this](const InputEvent& event, PointerCapture& capture) {
            return Workspace.Interaction.Dispatcher->OnInput(event, capture);
        });
        Actions = std::make_unique<EntityVerbs>(Workspace, std::function<bool(const std::string&)>{},
            [this](const std::string& problem) { Problem = problem; });
        Actions->Offer(Catalog, Bindings);
    }
    BrushTool& Brush()
    {
        for (const auto& tool : Workspace.Interaction.Tools->GetTools())
            if (tool->GetId() == "brush")
                return static_cast<BrushTool&>(*tool);
        std::abort();
    }
    Vec2d Pixel(Vec3d point)
    {
        return ViewportProjection(*Workspace.Layout.Find(Top)).WorldToPixel(point)->Pixel;
    }
    void Draw()
    {
        const auto start = Pixel({ 1, 0, 1 }), end = Pixel({ 5, 0, 7 });
        (void)Router.Route(PointerDownEvent{ .Position = start, .Button = MouseButton::Left,
            .Modifiers = {}, .Viewport = Top });
        (void)Router.Route(PointerMoveEvent{ .Position = end, .Delta = end - start,
            .Modifiers = {}, .Viewport = Top });
        (void)Router.Route(PointerUpEvent{ .Position = end, .Button = MouseButton::Left,
            .Modifiers = {}, .Viewport = Top });
    }
    void Invoke(std::string_view action)
    {
        const auto* binding = Bindings.Find(action);
        ASSERT_NE(binding, nullptr);
        ASSERT_TRUE(binding->TryInvoke());
    }
    PersistentEntityId IdOf(EntityId entity)
    {
        return Scene().GetRegistry().Components.TryGet<PersistentIdComponent>(entity)->Id;
    }
    EntityId Resolve(PersistentEntityId id)
    {
        return Scene().GetRegistry().Components.TryGetResource<PersistentEntityIndex>()->TryResolve(id);
    }
    ActionCatalog Catalog;
    ActionContext Bindings;
    std::unique_ptr<EntityVerbs> Actions;
    std::string Problem;
    InputRouter Router;
    ViewportId Top;
};
}

TEST_F(TriggerVolumeAuthoringTest, PaletteChoiceUsesOrdinaryBrushGestureAndOneUndoableCreation)
{
    Invoke("trigger.create");
    ASSERT_EQ(Workspace.Interaction.Tools->GetActiveTool()->GetId(), "brush");
    EXPECT_EQ(Brush().GetVariants()[Brush().GetActiveVariant(*Workspace.Interaction.Context)].Label, "Trigger Volume");
    Draw();
    ASSERT_TRUE(Brush().HasPending());
    const auto entity = Workspace.Selection.GetPrimarySelection().Entity;
    ASSERT_TRUE(entity.IsValid());
    auto& world = Scene().GetRegistry().Components;
    EXPECT_TRUE(world.HasComponent<BrushVolume>(entity));
    EXPECT_TRUE(world.HasComponent<Trigger>(entity));
    EXPECT_FALSE(world.HasComponent<Collider>(entity));
    EXPECT_EQ(JsonStringify(*TriggerProgramSource(world, entity)), JsonStringify(EmptyTriggerProgram()));
    EXPECT_TRUE(world.TryGet<EntityNameComponent>(entity)->Value.View().starts_with("Trigger"));
    const auto bounds = BrushComputeBounds(*Scene().TryGetBrushMesh(entity));
    EXPECT_NEAR(bounds.Max.X - bounds.Min.X, 4, 1e-5f);
    EXPECT_NEAR(bounds.Max.Z - bounds.Min.Z, 6, 1e-5f);
    EXPECT_TRUE(Commands.HasPendingEdit());
    const auto id = IdOf(entity);
    (void)Router.Route(KeyDownEvent{ .Key = SDLK_RETURN, .Modifiers = {} });
    EXPECT_FALSE(Brush().HasPending());
    ASSERT_TRUE(Commands.CanUndo());
    Commands.Undo();
    EXPECT_FALSE(Resolve(id).IsValid());
    EXPECT_EQ(BrushCount(), 0u);
    EXPECT_EQ(TriggerProgramsOf(world).Size(), 0u);
    Commands.Redo();
    const auto restored = Resolve(id);
    ASSERT_TRUE(restored.IsValid());
    EXPECT_TRUE(world.HasComponent<Trigger>(restored));
    EXPECT_EQ(TriggerProgramsOf(world).Size(), 1u);
    EXPECT_FALSE(Commands.CanRedo());
}

TEST_F(TriggerVolumeAuthoringTest, CancelRemovesTheWholePendingVolumeAndRestoresSelection)
{
    const auto previous = AddBrush({ -10, 0, 0 });
    SelectEntity(previous);
    Invoke("trigger.create");
    Draw();
    ASSERT_TRUE(Brush().HasPending());
    (void)Router.Route(KeyDownEvent{ .Key = SDLK_ESCAPE, .Modifiers = {} });
    EXPECT_FALSE(Brush().HasPending());
    EXPECT_EQ(BrushCount(), 1u);
    EXPECT_EQ(Workspace.Selection.GetPrimarySelection().Entity, previous);
    EXPECT_FALSE(Commands.CanUndo());
    EXPECT_EQ(TriggerProgramsOf(Scene().GetRegistry().Components).Size(), 0u);
}

TEST_F(TriggerVolumeAuthoringTest, ConversionPreservesGeometryTransformIdentityAndUndoRestoresSolidRole)
{
    const auto entity = AddBrush({ 3, 2, 1 }, { 2, 1, 4 });
    const auto id = IdOf(entity);
    const auto geometry = BrushGeometrySignature(*Scene().TryGetBrushMesh(entity));
    const auto pose = *Scene().TryGetLocalTransform(entity);
    SelectEntity(entity);
    Invoke("trigger.convert");
    EXPECT_TRUE(Problem.empty());
    EXPECT_TRUE(Scene().GetRegistry().Components.HasComponent<BrushVolume>(entity));
    EXPECT_EQ(BrushGeometrySignature(*Scene().TryGetBrushMesh(entity)), geometry);
    EXPECT_EQ(*Scene().TryGetLocalTransform(entity), pose);
    EXPECT_EQ(IdOf(entity), id);
    Commands.Undo();
    EXPECT_FALSE(Scene().GetRegistry().Components.HasComponent<Trigger>(Resolve(id)));
    EXPECT_FALSE(Scene().GetRegistry().Components.HasComponent<BrushVolume>(Resolve(id)));
    Commands.Redo();
    EXPECT_TRUE(Scene().GetRegistry().Components.HasComponent<Trigger>(Resolve(id)));
}

TEST_F(TriggerVolumeAuthoringTest, LosingFocusCancelsThePendingVolumeAndItsProgram)
{
    Invoke("trigger.create");
    Draw();
    ASSERT_TRUE(Brush().HasPending());
    (void)Router.Route(FocusLostEvent{});
    EXPECT_FALSE(Brush().HasPending());
    EXPECT_EQ(BrushCount(), 0u);
    EXPECT_EQ(TriggerProgramsOf(Scene().GetRegistry().Components).Size(), 0u);
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(TriggerVolumeAuthoringTest, UndoCancelsAPendingVolumeWithoutCreatingARedoStep)
{
    Invoke("trigger.create");
    Draw();
    ASSERT_TRUE(Brush().HasPending());
    Commands.Undo();
    EXPECT_FALSE(Brush().HasPending());
    EXPECT_EQ(BrushCount(), 0u);
    EXPECT_FALSE(Commands.CanUndo());
    EXPECT_FALSE(Commands.CanRedo());
    EXPECT_EQ(TriggerProgramsOf(Scene().GetRegistry().Components).Size(), 0u);
}

TEST_F(TriggerVolumeAuthoringTest, SwitchingToolsCommitsThePendingVolumeAsAnOrdinaryBrush)
{
    Invoke("trigger.create");
    Draw();
    ASSERT_TRUE(Brush().HasPending());
    ASSERT_TRUE(Workspace.Interaction.Tools->Activate("select"));
    EXPECT_FALSE(Brush().HasPending());
    EXPECT_EQ(BrushCount(), 1u);
    EXPECT_TRUE(Commands.CanUndo());
    Commands.Undo();
    EXPECT_EQ(BrushCount(), 0u);
    EXPECT_EQ(TriggerProgramsOf(Scene().GetRegistry().Components).Size(), 0u);
}

TEST_F(TriggerVolumeAuthoringTest, DuplicateHasIndependentIdentityAndProgramAndRetainsUnknownData)
{
    const auto original = AddBrush({});
    SelectEntity(original);
    Invoke("trigger.convert");
    auto& world = Scene().GetRegistry().Components;
    auto source = EmptyTriggerProgram();
    source.Find("commands")->AsArray().emplace_back(JsonValue::Object{
        { "enabled", false }, { "verb", "game.unavailable" }, { "future", JsonValue::Array{ 1, 2, 3 } } });
    ASSERT_TRUE(SetTriggerProgram(world, original, source));
    Workspace.Actions.Duplicate(false);
    const auto copy = Workspace.Selection.GetPrimarySelection().Entity;
    ASSERT_NE(copy, original);
    ASSERT_TRUE(world.HasComponent<Trigger>(copy));
    EXPECT_NE(IdOf(copy), IdOf(original));
    EXPECT_NE(world.TryGet<Trigger>(copy)->Program, world.TryGet<Trigger>(original)->Program);
    EXPECT_EQ(JsonStringify(*TriggerProgramSource(world, copy)), JsonStringify(source));
    ASSERT_TRUE(SetTriggerProgram(world, copy, EmptyTriggerProgram()));
    EXPECT_EQ(JsonStringify(*TriggerProgramSource(world, original)), JsonStringify(source));
    Commands.Undo();
    EXPECT_EQ(TriggerProgramsOf(world).Size(), 1u);
    Commands.Redo();
    EXPECT_EQ(TriggerProgramsOf(world).Size(), 2u);
}

TEST_F(TriggerVolumeAuthoringTest, DrawnVolumeRoundTripsThroughTheSceneDocumentWithUnknownCommands)
{
    Invoke("trigger.create");
    Draw();
    ASSERT_TRUE(Brush().HasPending());
    (void)Router.Route(KeyDownEvent{ .Key = SDLK_RETURN, .Modifiers = {} });
    const auto entity = Workspace.Selection.GetPrimarySelection().Entity;
    const auto id = IdOf(entity);
    const auto geometry = BrushGeometrySignature(*Scene().TryGetBrushMesh(entity));
    auto source = EmptyTriggerProgram();
    source.Find("commands")->AsArray().emplace_back(JsonValue::Object{
        { "enabled", true }, { "verb", "game.unavailable" },
        { "args", JsonValue::Object{ { "future", JsonValue::Array{ "kept", 42, false } } } } });
    ASSERT_TRUE(SetTriggerProgram(Scene().GetRegistry().Components, entity, source));
    EditorDocument loaded(Logging);
    std::string error;
    ASSERT_TRUE(loaded.LoadFromSceneText(Workspace.ActiveDocument().ToSceneText(), &error)) << error;
    const auto& world = loaded.GetRegistry().Components;
    const auto restored = world.TryGetResource<PersistentEntityIndex>()->TryResolve(id);
    ASSERT_TRUE(restored.IsValid());
    EXPECT_TRUE(world.HasComponent<BrushVolume>(restored));
    EXPECT_FALSE(world.HasComponent<Collider>(restored));
    EXPECT_EQ(BrushGeometrySignature(*loaded.GetScene().TryGetBrushMesh(restored)), geometry);
    EXPECT_EQ(JsonStringify(*TriggerProgramSource(world, restored)), JsonStringify(source));
}

TEST_F(TriggerVolumeAuthoringTest, IncompatibleConversionReportsItsOwnerAndChangesNothing)
{
    const auto entity = AddBrush({});
    auto& world = Scene().GetRegistry().Components;
    world.AddComponent(entity, BrushVolume{});
    world.AddComponent(entity, Space{});
    SelectEntity(entity);
    Invoke("trigger.convert");
    EXPECT_NE(Problem.find("another owner"), std::string::npos);
    EXPECT_TRUE(world.HasComponent<Space>(entity));
    EXPECT_FALSE(world.HasComponent<Trigger>(entity));
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(TriggerVolumeAuthoringTest, ReverseConversionRefusesToDiscardEvenDisabledUnknownCommands)
{
    const auto entity = AddBrush({});
    SelectEntity(entity);
    Invoke("trigger.convert");
    auto& world = Scene().GetRegistry().Components;
    auto source = EmptyTriggerProgram();
    source.Find("commands")->AsArray().emplace_back(JsonValue::Object{
        { "enabled", false }, { "verb", "game.unavailable" }, { "future", JsonValue::Array{ 1, 2, 3 } } });
    ASSERT_TRUE(SetTriggerProgram(world, entity, source));
    Invoke("trigger.make_solid");
    EXPECT_NE(Problem.find("authored data"), std::string::npos);
    EXPECT_EQ(JsonStringify(*TriggerProgramSource(world, entity)), JsonStringify(source));
    EXPECT_TRUE(world.HasComponent<BrushVolume>(entity));
    ASSERT_TRUE(SetTriggerProgram(world, entity, EmptyTriggerProgram()));
    Problem.clear();
    Invoke("trigger.make_solid");
    EXPECT_TRUE(Problem.empty());
    EXPECT_FALSE(world.HasComponent<Trigger>(entity));
    EXPECT_FALSE(world.HasComponent<BrushVolume>(entity));
    Commands.Undo();
    EXPECT_TRUE(world.HasComponent<Trigger>(entity));
}
