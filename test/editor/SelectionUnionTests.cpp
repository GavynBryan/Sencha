#include "WorkspaceFixture.h"

#include "authoring/ComponentPropertySource.h"
#include "properties/PropertyEditSession.h"

#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <algorithm>

// Several selected objects are edited as one: every component any of them
// holds, a component only some hold saying so, a field they disagree on
// standing for all of them. Setting sets every holder alike, moving by an
// amount moves each from its own, and whatever an edit touches is one step.
namespace
{
class SelectionUnionTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        First = Scene().CreateEntity({ 0, 2, 0 });
        Second = Scene().CreateEntity({ 1, 2, 3 });
        Third = Scene().CreateEntity({ 4, 2, 6 });
        Light(First, 4.0f);
        Light(Second, 6.0f);
        Select({ SelectableRef::EntitySelection(Registry(), First), SelectableRef::EntitySelection(Registry(), Second),
                 SelectableRef::EntitySelection(Registry(), Third) });
    }

    void Light(EntityId entity, float range)
    {
        PointLightComponent light;
        light.Range = range;
        Scene().GetRegistry().Components.AddComponent(entity, light);
    }

    [[nodiscard]] const PointLightComponent* LightOf(EntityId entity)
    {
        return Scene().GetRegistry().Components.TryGet<PointLightComponent>(entity);
    }
    [[nodiscard]] Vec3d PositionOf(EntityId entity)
    {
        return Scene().GetRegistry().Components.TryGet<LocalTransform>(entity)->Value.Position;
    }

    [[nodiscard]] std::optional<PropertyNode> Find(std::string_view key) const
    {
        const auto search = [&](const auto& self, const std::vector<PropertyNode>& level) -> std::optional<PropertyNode> {
            for (const PropertyNode& node : level)
            {
                if (node.Key == key)
                    return node;
                if (std::optional<PropertyNode> found = self(self, node.Children))
                    return found;
            }
            return std::nullopt;
        };
        return search(search, Components.Nodes());
    }

    [[nodiscard]] std::string PositionKey() const
    {
        for (const PropertyNode& group : Components.Nodes())
            for (const PropertyNode& field : group.Children)
                if (field.Descriptor.Label == "Position")
                    return field.Key;
        return {};
    }

    EntityId First{};
    EntityId Second{};
    EntityId Third{};
    ComponentPropertySource Components{ Workspace.World, Workspace.Selection, Commands,
                                        Workspace.Affordances->Registry() };
};
} // namespace

TEST_F(SelectionUnionTest, AComponentOnSomeSaysHowManyAndOffersItToTheRest)
{
    const std::optional<PropertyNode> light = Find("PointLight");
    ASSERT_TRUE(light.has_value());
    EXPECT_NE(light->Descriptor.Label.find("on 2 of 3"), std::string::npos) << light->Descriptor.Label;
    const auto spread = std::ranges::find(light->Descriptor.Verbs, "spread", &PropertyVerb::Id);
    ASSERT_NE(spread, light->Descriptor.Verbs.end());
    EXPECT_EQ(spread->Label, "Add to the other 1");
    EXPECT_EQ(Find("Transform")->Descriptor.Label, "Transform") << "all three hold it";
}

TEST_F(SelectionUnionTest, AFieldTheyDisagreeOnStandsForAllAndOneTheyShareShowsIt)
{
    const std::optional<PropertyNode> range = Find("PointLight/range");
    ASSERT_TRUE(range.has_value());
    EXPECT_TRUE(range->Mixed);
    EXPECT_TRUE(range->Value.empty());
    EXPECT_FALSE(Find("PointLight/intensity")->Mixed);

    const std::optional<PropertyNode> position = Find(PositionKey());
    ASSERT_TRUE(position.has_value());
    EXPECT_TRUE(position->Mixed);
    ASSERT_EQ(position->Children.size(), 3u);
    EXPECT_TRUE(position->Children[0].Mixed);
    EXPECT_FALSE(position->Children[1].Mixed) << "every one stands at height 2";
    EXPECT_EQ(position->Children[1].Value, "2");
}

TEST_F(SelectionUnionTest, SettingAValueSetsEveryHolderAsOneStep)
{
    EXPECT_EQ(PropertyEditSession{}.Set(Components, "PointLight/range", "5"), PropertyEditResult::Applied);
    EXPECT_FLOAT_EQ(LightOf(First)->Range, 5.0f);
    EXPECT_FLOAT_EQ(LightOf(Second)->Range, 5.0f);
    EXPECT_EQ(LightOf(Third), nullptr) << "a value lands only where the component is";
    Commands.Undo();
    EXPECT_FLOAT_EQ(LightOf(First)->Range, 4.0f);
    EXPECT_FLOAT_EQ(LightOf(Second)->Range, 6.0f);
}

TEST_F(SelectionUnionTest, MovingByAnAmountMovesEachFromItsOwnAsOneStep)
{
    PropertyEditSession session;
    ASSERT_TRUE(session.Begin(Components, "PointLight/range"));
    EXPECT_EQ(session.PreviewBy(1.0), PropertyEditResult::Applied);
    EXPECT_FLOAT_EQ(LightOf(First)->Range, 5.0f) << "previewed live";
    EXPECT_FLOAT_EQ(LightOf(Second)->Range, 7.0f);
    EXPECT_EQ(session.CommitBy(1.5), PropertyEditResult::Applied);
    EXPECT_FLOAT_EQ(LightOf(First)->Range, 5.5f);
    EXPECT_FLOAT_EQ(LightOf(Second)->Range, 7.5f);
    Commands.Undo();
    EXPECT_FLOAT_EQ(LightOf(First)->Range, 4.0f);
    EXPECT_FLOAT_EQ(LightOf(Second)->Range, 6.0f);
}

TEST_F(SelectionUnionTest, AnAbandonedEditPutsEveryHolderBack)
{
    PropertyEditSession session;
    ASSERT_TRUE(session.Begin(Components, "PointLight/range"));
    (void)session.PreviewBy(2.0);
    session.Cancel();
    EXPECT_FLOAT_EQ(LightOf(First)->Range, 4.0f);
    EXPECT_FLOAT_EQ(LightOf(Second)->Range, 6.0f);
    EXPECT_FALSE(Commands.CanUndo());
}

TEST_F(SelectionUnionTest, OneNumberOfAVectorChangesAloneAndKeepsEachOnesOthers)
{
    const std::string x = PositionKey() + "[0]";
    EXPECT_EQ(PropertyEditSession{}.Set(Components, x, "9"), PropertyEditResult::Applied);
    EXPECT_FLOAT_EQ(PositionOf(First).X, 9.0f);
    EXPECT_FLOAT_EQ(PositionOf(Third).X, 9.0f);
    EXPECT_FLOAT_EQ(PositionOf(Second).Z, 3.0f);
    EXPECT_FLOAT_EQ(PositionOf(Third).Z, 6.0f);
    EXPECT_FALSE(Find(PositionKey())->Children[0].Mixed);
}

TEST_F(SelectionUnionTest, RemovingAndSpreadingActOnTheSelectionAsOneStep)
{
    EXPECT_EQ(Components.Act("PointLight", "spread", {}), PropertyEditResult::Applied);
    EXPECT_NE(LightOf(Third), nullptr);
    Commands.Undo();
    EXPECT_EQ(LightOf(Third), nullptr);

    EXPECT_EQ(Components.Act("PointLight", "remove", {}), PropertyEditResult::Applied);
    EXPECT_EQ(LightOf(First), nullptr);
    EXPECT_EQ(LightOf(Second), nullptr);
    Commands.Undo();
    EXPECT_NE(LightOf(First), nullptr);
    EXPECT_NE(LightOf(Second), nullptr) << "one undo brings back both";
}

TEST_F(SelectionUnionTest, AddingAComponentGivesItToEveryOneAsOneStep)
{
    EXPECT_EQ(Components.Act("components", "add", "SpotLight"), PropertyEditResult::Applied);
    for (const EntityId entity : { First, Second, Third })
        EXPECT_NE(Scene().GetRegistry().Components.TryGet<SpotLightComponent>(entity), nullptr);
    Commands.Undo();
    for (const EntityId entity : { First, Second, Third })
        EXPECT_EQ(Scene().GetRegistry().Components.TryGet<SpotLightComponent>(entity), nullptr);
}

TEST_F(SelectionUnionTest, AComponentAnAdapterPresentsIsEditedOneObjectAtATime)
{
    const EntityId one = AddBrush({ 0, 0, 0 });
    const EntityId two = AddBrush({ 3, 0, 0 });
    Select({ SelectableRef::EntitySelection(Registry(), one), SelectableRef::EntitySelection(Registry(), two) });
    const std::optional<PropertyNode> brush = Find("brush");
    ASSERT_TRUE(brush.has_value());
    EXPECT_EQ(Find("brush:stack"), std::nullopt) << "two stacks have no one stack to show";
    EXPECT_EQ(Components.Act("brush:stack", "add", "Array"), PropertyEditResult::Refused);

    SelectEntity(one);
    EXPECT_TRUE(Find("brush:stack").has_value());
}
