#include "WorkspaceFixture.h"

#include "document/EntityNameComponent.h"
#include "document/commands/PlaceEntityCommand.h"
#include "document/tools/PlaceTool.h"
#include "tools/ToolRegistry.h"

#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

// The Entity tool puts down the prefab in hand, an empty entity, or a new
// entity carrying one component that declares itself placeable, as one step
// with it selected.

namespace
{
class PlaceToolTest : public WorkspaceTest
{
protected:
    [[nodiscard]] std::vector<std::string> VariantLabels(const PlaceTool& tool) const
    {
        std::vector<std::string> labels;
        for (const ITool::Variant& variant : tool.GetVariants())
            labels.emplace_back(variant.Label);
        return labels;
    }

    [[nodiscard]] std::size_t IndexOf(const PlaceTool& tool, std::string_view label) const
    {
        const std::vector<std::string> labels = VariantLabels(tool);
        return static_cast<std::size_t>(std::ranges::find(labels, label) - labels.begin());
    }

    [[nodiscard]] Transform3f At(Vec3d position) const
    {
        Transform3f placement = Transform3f::Identity();
        placement.Position = position;
        return placement;
    }
};
} // namespace

TEST_F(PlaceToolTest, ItsVariantsAreThePrefabInHandAnEmptyAndEveryPlaceableComponent)
{
    const PlaceTool place;
    const std::vector<std::string> labels = VariantLabels(place);
    ASSERT_GE(labels.size(), 4u);
    EXPECT_EQ(labels[0], "Prefab");
    EXPECT_EQ(labels[1], "Empty");
    EXPECT_TRUE(std::is_sorted(labels.begin() + 2, labels.end())) << "the wheel's order depends on load order";

    // Each pictured, by the art its component names where it is one.
    const auto variants = place.GetVariants();
    EXPECT_EQ(variants[1].Icon, IconId::Empty);
    EXPECT_EQ(variants[IndexOf(place, "Point Light")].Icon, IconId::LightBulb);
    EXPECT_EQ(variants[IndexOf(place, "Spot Light")].Icon, IconId::LightSpot);
    for (const ITool::Variant& variant : variants)
        EXPECT_NE(variant.Icon, IconId::None) << variant.Label;
}

TEST_F(PlaceToolTest, AComponentVariantNeedsNoPrefabInHand)
{
    PlaceTool place;
    ToolContext& ctx = Workspace.Interaction.Tools->GetContext();
    EXPECT_EQ(place.GetActiveVariant(ctx), 0);
    ASSERT_EQ(place.Resources().size(), 1u);

    place.SelectVariant(ctx, IndexOf(place, "Point Light"));
    EXPECT_EQ(place.GetVariants()[static_cast<std::size_t>(place.GetActiveVariant(ctx))].Label, "Point Light");
    EXPECT_TRUE(place.Resources().empty()) << "a light was asked to be placed and a prefab cartridge offered";
    EXPECT_EQ(place.Hint(ctx), "Click to place a Point Light");
}

TEST_F(PlaceToolTest, APlacedComponentIsOneStepNamedAndSelected)
{
    World& world = Scene().GetRegistry().Components;
    const std::size_t before = Scene().GetAllEntities().size();
    Workspace.Commands.Execute(std::make_unique<PlaceEntityCommand>(
        "PointLight", At({ 2.0f, 1.0f, -3.0f }), Workspace.ActiveDocument(), Workspace.Selection));

    ASSERT_EQ(Scene().GetAllEntities().size(), before + 1);
    const EntityId light = Scene().GetAllEntities().back();
    ASSERT_NE(world.TryGet<PointLightComponent>(light), nullptr);
    EXPECT_EQ(world.TryGet<PointLightComponent>(light)->Range, PointLightComponent{}.Range) << "not placed at its defaults";
    ASSERT_NE(world.TryGet<LocalTransform>(light), nullptr);
    EXPECT_EQ(world.TryGet<LocalTransform>(light)->Value.Position.X, 2.0f);
    ASSERT_NE(world.TryGet<EntityNameComponent>(light), nullptr);
    EXPECT_EQ(std::string(world.TryGet<EntityNameComponent>(light)->Value.View()), "Point Light");
    ASSERT_EQ(Workspace.Selection.GetSelection().size(), 1u);
    EXPECT_EQ(Workspace.Selection.GetPrimarySelection().Entity, light);

    Workspace.Commands.Undo();
    EXPECT_EQ(Scene().GetAllEntities().size(), before) << "undo left the light behind";
    EXPECT_TRUE(Workspace.Selection.GetSelection().empty());
}

TEST_F(PlaceToolTest, AnEmptyIsOneStepNamedAndSelected)
{
    PlaceTool place;
    ToolContext& ctx = Workspace.Interaction.Tools->GetContext();
    place.SelectVariant(ctx, IndexOf(place, "Empty"));
    EXPECT_TRUE(place.Resources().empty()) << "an empty was asked to be placed and a prefab cartridge offered";
    EXPECT_EQ(place.Hint(ctx), "Click to place an Empty");

    World& world = Scene().GetRegistry().Components;
    const std::size_t before = Scene().GetAllEntities().size();
    Workspace.Commands.Execute(
        std::make_unique<PlaceEntityCommand>("", At({ 1.0f, 0.0f, 4.0f }), Workspace.ActiveDocument(), Workspace.Selection));

    ASSERT_EQ(Scene().GetAllEntities().size(), before + 1);
    const EntityId empty = Scene().GetAllEntities().back();
    ASSERT_NE(world.TryGet<LocalTransform>(empty), nullptr);
    EXPECT_EQ(world.TryGet<LocalTransform>(empty)->Value.Position.Z, 4.0f);
    ASSERT_NE(world.TryGet<EntityNameComponent>(empty), nullptr);
    EXPECT_EQ(std::string(world.TryGet<EntityNameComponent>(empty)->Value.View()), "Entity");
    EXPECT_NE(world.TryGet<PersistentIdComponent>(empty), nullptr);
    EXPECT_EQ(world.TryGet<PointLightComponent>(empty), nullptr);
    EXPECT_EQ(Workspace.Selection.GetPrimarySelection().Entity, empty);

    Workspace.Commands.Undo();
    EXPECT_EQ(Scene().GetAllEntities().size(), before);
}

TEST_F(PlaceToolTest, ARedoPlacesTheSameEntityAgain)
{
    Workspace.Commands.Execute(std::make_unique<PlaceEntityCommand>(
        "SpotLight", At({}), Workspace.ActiveDocument(), Workspace.Selection));
    World& world = Scene().GetRegistry().Components;
    const PersistentIdComponent placed = *world.TryGet<PersistentIdComponent>(Scene().GetAllEntities().back());

    Workspace.Commands.Undo();
    Workspace.Commands.Redo();
    const EntityId again = Scene().GetAllEntities().back();
    ASSERT_NE(world.TryGet<SpotLightComponent>(again), nullptr);
    EXPECT_EQ(world.TryGet<PersistentIdComponent>(again)->Id, placed.Id)
        << "a redo minted a new identity, losing anything that named the first";
}

TEST_F(PlaceToolTest, AComponentThatIsNotPlaceableIsRefused)
{
    const std::size_t before = Scene().GetAllEntities().size();
    Workspace.Commands.Execute(std::make_unique<PlaceEntityCommand>(
        "Transform", At({}), Workspace.ActiveDocument(), Workspace.Selection));
    EXPECT_EQ(Scene().GetAllEntities().size(), before);
}
