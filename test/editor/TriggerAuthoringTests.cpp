// A trigger authored the way a designer does it: a brush made a trigger
// volume in one step, its launch configured through the inspector, every step
// undoable, and the level saving and loading what was configured.

#include "WorkspaceFixture.h"

#include "authoring/ComponentPropertySource.h"
#include "document/BrushComponents.h"
#include "document/EditorDocument.h"

#include <core/json/JsonStringify.h>
#include <logic/Trigger.h>
#include <logic/TriggerSequences.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace
{
constexpr std::array<std::string_view, 3> kTriggerVolume = { "volume", "volume_sensor", "trigger" };

class TriggerAuthoringTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Inspector = std::make_unique<ComponentPropertySource>(Workspace.World, Workspace.Selection,
                                                              Workspace.Commands, Workspace.Affordances->Registry());
    }

    ::World& Entities() { return Scene().GetRegistry().Components; }

    std::string Sequence(EntityId entity)
    {
        const JsonValue* sequence = TriggerSequenceSource(Entities(), entity);
        return sequence != nullptr ? JsonStringify(*sequence) : std::string();
    }

    static const PropertyNode* Find(const std::vector<PropertyNode>& nodes, std::string_view key)
    {
        for (const PropertyNode& node : nodes)
        {
            if (node.Key == key)
                return &node;
            if (const PropertyNode* child = Find(node.Children, key))
                return child;
        }
        return nullptr;
    }

    PropertyEditResult Set(std::string_view key, std::string_view value)
    {
        return Inspector->CommitEdit(key, value);
    }

    std::unique_ptr<ComponentPropertySource> Inspector;
};
} // namespace

TEST_F(TriggerAuthoringTest, ABrushBecomesATriggerVolumeInOneUndoableStep)
{
    const EntityId brush = AddBrush({ 0, 0.5, 0 }, { 1, 0.5, 1 });
    SelectEntity(brush);
    Workspace.Actions.AddComponentsToBrushes(kTriggerVolume);
    EXPECT_TRUE(Entities().HasComponent<BrushVolume>(brush));
    EXPECT_TRUE(Entities().HasComponent<VolumeSensor>(brush));
    EXPECT_TRUE(Entities().HasComponent<Trigger>(brush));

    Workspace.Commands.Undo();
    EXPECT_FALSE(Entities().HasComponent<BrushVolume>(brush));
    EXPECT_FALSE(Entities().HasComponent<Trigger>(brush));
    Workspace.Commands.Redo();
    EXPECT_TRUE(Entities().HasComponent<Trigger>(brush));
}

TEST_F(TriggerAuthoringTest, ALaunchIsConfiguredInTheInspectorBesideThePolicyFields)
{
    const EntityId brush = AddBrush({ 0, 0.5, 0 }, { 1, 0.5, 1 });
    SelectEntity(brush);
    Workspace.Actions.AddComponentsToBrushes(kTriggerVolume);

    std::vector<PropertyNode> rows = Inspector->Nodes();
    const PropertyNode* trigger = Find(rows, "trigger");
    ASSERT_NE(trigger, nullptr);
    // The policy is the schema's own rows; the sequence follows them.
    EXPECT_NE(std::ranges::find_if(trigger->Children, [](const PropertyNode& node) {
                  return node.Descriptor.Label == "Activates on";
              }), trigger->Children.end());
    ASSERT_NE(Find(rows, "trigger:commands"), nullptr);

    ASSERT_EQ(Inspector->Act("trigger:commands", "add", "movement.launch"), PropertyEditResult::Applied);
    ASSERT_EQ(Set("trigger:k0.arg.direction.value", "0, 0, 1"), PropertyEditResult::Applied);
    ASSERT_EQ(Set("trigger:k0.arg.direction.frame", "trigger"), PropertyEditResult::Applied);
    ASSERT_EQ(Set("trigger:k0.arg.speed.value", "12"), PropertyEditResult::Applied);
    ASSERT_EQ(Set("trigger:k0.arg.rise.value", "6"), PropertyEditResult::Applied);
    const std::string configured = Sequence(brush);
    EXPECT_NE(configured.find("movement.launch"), std::string::npos);

    // Each edit was its own step, and undoing walks back through them.
    for (int i = 0; i < 4; ++i)
        Workspace.Commands.Undo();
    EXPECT_EQ(Sequence(brush).find("\"speed\""), std::string::npos);
    EXPECT_NE(Sequence(brush).find("movement.launch"), std::string::npos);
    for (int i = 0; i < 4; ++i)
        Workspace.Commands.Redo();
    EXPECT_EQ(Sequence(brush), configured);

    // No problems: the volume senses and the launch resolves.
    rows = Inspector->Nodes();
    EXPECT_EQ(Find(rows, "trigger:problems"), nullptr);
}

TEST_F(TriggerAuthoringTest, AVolumeThatSensesNothingSaysSo)
{
    const EntityId brush = AddBrush({ 0, 0.5, 0 }, { 1, 0.5, 1 });
    SelectEntity(brush);
    static constexpr std::array<std::string_view, 2> kNoSensor = { "volume", "trigger" };
    Workspace.Actions.AddComponentsToBrushes(kNoSensor);
    const std::vector<PropertyNode> rows = Inspector->Nodes();
    const PropertyNode* problems = Find(rows, "trigger:problems");
    ASSERT_NE(problems, nullptr);
    ASSERT_FALSE(problems->Children.empty());
    EXPECT_NE(problems->Children.front().Value.find("sensor"), std::string::npos);
}

TEST_F(TriggerAuthoringTest, TheConfiguredTriggerSavesAndLoads)
{
    const EntityId brush = AddBrush({ 0, 0.5, 0 }, { 1, 0.5, 1 });
    SelectEntity(brush);
    Workspace.Actions.AddComponentsToBrushes(kTriggerVolume);
    ASSERT_EQ(Inspector->Act("trigger:commands", "add", "movement.launch"), PropertyEditResult::Applied);
    ASSERT_EQ(Set("trigger:k0.arg.speed.value", "9"), PropertyEditResult::Applied);
    Entities().TryGet<Trigger>(brush)->CooldownSeconds = 2.0f;
    const std::string configured = Sequence(brush);

    const std::string text = Workspace.ActiveDocument().ToSceneText();
    EditorDocument loaded(Logging);
    ASSERT_TRUE(loaded.LoadFromSceneText(text));
    const ::World& world = loaded.GetRegistry().Components;
    int triggers = 0;
    for (const EntityId entity : loaded.GetScene().GetAllEntities())
        if (const Trigger* trigger = world.TryGet<Trigger>(entity))
        {
            ++triggers;
            EXPECT_FLOAT_EQ(trigger->CooldownSeconds, 2.0f);
            EXPECT_TRUE(world.HasComponent<VolumeSensor>(entity));
            EXPECT_TRUE(world.HasComponent<BrushVolume>(entity));
            EXPECT_EQ(JsonStringify(*TriggerSequenceSource(world, entity)), configured);
        }
    EXPECT_EQ(triggers, 1);
}
