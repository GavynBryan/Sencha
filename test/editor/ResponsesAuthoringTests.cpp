// Responses authored the way a designer does it: a brush made a trigger volume
// in one step, its launch configured through the inspector on the response it
// offers, every step undoable, and the level saving and loading what was
// configured. The same component on anything else is that entity's Behaviors.

#include "WorkspaceFixture.h"

#include "authoring/ComponentPropertySource.h"
#include "document/BrushComponents.h"
#include "document/EditorDocument.h"

#include <core/json/JsonStringify.h>
#include <logic/ResponseSet.h>
#include <logic/Responses.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace
{
constexpr std::array<std::string_view, 3> kTriggerVolume = { "volume", "volume_sensor", "responses" };

class ResponsesAuthoringTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Inspector = std::make_unique<ComponentPropertySource>(Workspace.World, Workspace.Selection,
                                                              Workspace.Commands, Workspace.Affordances->Registry());
    }

    ::World& Entities() { return Scene().GetRegistry().Components; }

    // One binding's sequence as saved, or nothing.
    std::string Sequence(EntityId entity, std::string_view binding = "enter")
    {
        const JsonValue* bindings = ResponseBindingsSource(Entities(), entity);
        const JsonValue* found = bindings != nullptr ? bindings->Find(binding) : nullptr;
        const JsonValue* sequence = found != nullptr ? found->Find("sequence") : nullptr;
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

TEST_F(ResponsesAuthoringTest, ABrushBecomesATriggerVolumeInOneUndoableStep)
{
    const EntityId brush = AddBrush({ 0, 0.5, 0 }, { 1, 0.5, 1 });
    SelectEntity(brush);
    Workspace.Actions.AddComponentsToBrushes(kTriggerVolume);
    EXPECT_TRUE(Entities().HasComponent<BrushVolume>(brush));
    EXPECT_TRUE(Entities().HasComponent<VolumeSensor>(brush));
    EXPECT_TRUE(Entities().HasComponent<Responses>(brush));

    Workspace.Commands.Undo();
    EXPECT_FALSE(Entities().HasComponent<BrushVolume>(brush));
    EXPECT_FALSE(Entities().HasComponent<Responses>(brush));
    Workspace.Commands.Redo();
    EXPECT_TRUE(Entities().HasComponent<Responses>(brush));
}

TEST_F(ResponsesAuthoringTest, ATriggerVolumeOffersAnEnterResponseAndALaunchIsConfiguredOnIt)
{
    const EntityId brush = AddBrush({ 0, 0.5, 0 }, { 1, 0.5, 1 });
    SelectEntity(brush);
    Workspace.Actions.AddComponentsToBrushes(kTriggerVolume);

    std::vector<PropertyNode> rows = Inspector->Nodes();
    const PropertyNode* group = Find(rows, "responses");
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->Descriptor.Label, "Trigger") << "a volume's responses are a level trigger";
    const PropertyNode* enter = Find(rows, "responses:b.enter");
    ASSERT_NE(enter, nullptr) << "a new trigger volume offered no response to start from";
    EXPECT_EQ(Find(rows, "responses:b.enter.on")->Value, "sensor.enter");
    EXPECT_TRUE(Sequence(brush).empty()) << "offering the response wrote it before anything was edited";
    ASSERT_NE(Find(rows, "responses:b.enter.s.commands"), nullptr);

    ASSERT_EQ(Inspector->Act("responses:b.enter.s.commands", "add", "movement.launch"), PropertyEditResult::Applied);
    ASSERT_EQ(Set("responses:b.enter.s.k0.arg.direction.value", "1, 0, 0"), PropertyEditResult::Applied);
    ASSERT_EQ(Set("responses:b.enter.s.k0.arg.direction.frame", "self"), PropertyEditResult::Applied);
    ASSERT_EQ(Set("responses:b.enter.s.k0.arg.speed.value", "12"), PropertyEditResult::Applied);
    ASSERT_EQ(Set("responses:b.enter.s.k0.arg.rise.value", "6"), PropertyEditResult::Applied);
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
    EXPECT_EQ(Find(rows, "responses:problems"), nullptr);
}

TEST_F(ResponsesAuthoringTest, AVolumeThatSensesNothingSaysSo)
{
    const EntityId brush = AddBrush({ 0, 0.5, 0 }, { 1, 0.5, 1 });
    SelectEntity(brush);
    static constexpr std::array<std::string_view, 2> kNoSensor = { "volume", "responses" };
    Workspace.Actions.AddComponentsToBrushes(kNoSensor);
    const std::vector<PropertyNode> rows = Inspector->Nodes();
    const PropertyNode* problems = Find(rows, "responses:problems");
    ASSERT_NE(problems, nullptr);
    ASSERT_FALSE(problems->Children.empty());
    EXPECT_NE(problems->Children.front().Value.find("sensor"), std::string::npos);
}

TEST_F(ResponsesAuthoringTest, ATriggerVolumeAnswersSeveralOccurrencesWithoutChildEntities)
{
    const EntityId brush = AddBrush({ 0, 0.5, 0 }, { 1, 0.5, 1 });
    SelectEntity(brush);
    Workspace.Actions.AddComponentsToBrushes(kTriggerVolume);
    ASSERT_EQ(Inspector->Act("responses:bindings", "add", "sensor.exit"), PropertyEditResult::Applied);
    ASSERT_EQ(Inspector->Act("responses:bindings", "add", "sensor.exit"), PropertyEditResult::Applied);
    ASSERT_EQ(Set("responses:b.exit.once", "true"), PropertyEditResult::Applied);

    const std::vector<PropertyNode> rows = Inspector->Nodes();
    EXPECT_NE(Find(rows, "responses:b.enter"), nullptr) << "adding a response dropped the one offered";
    EXPECT_NE(Find(rows, "responses:b.exit"), nullptr);
    EXPECT_NE(Find(rows, "responses:b.exit_2"), nullptr) << "a second binding to one event was refused";
    const std::span<const ResponseBindingDesc> bindings =
        ResponseDefinitionsOf(Entities()).Sets.Bindings(Entities().TryGet<Responses>(brush)->Set);
    ASSERT_EQ(bindings.size(), 3u);
    EXPECT_TRUE(bindings[1].Once);
    EXPECT_FALSE(bindings[2].Once) << "a policy edit reached another binding";

    ASSERT_EQ(Inspector->Act("responses:b.exit_2", "remove", ""), PropertyEditResult::Applied);
    EXPECT_EQ(ResponseDefinitionsOf(Entities()).Sets.Bindings(Entities().TryGet<Responses>(brush)->Set).size(), 2u);
}

TEST_F(ResponsesAuthoringTest, ResponsesOnAnythingElseAreItsBehaviorsAndOfferNoSensorEvents)
{
    const EntityId brush = AddBrush({ 0, 0.5, 0 }, { 1, 0.5, 1 });
    SelectEntity(brush);
    static constexpr std::array<std::string_view, 1> kBehaviors = { "responses" };
    Workspace.Actions.AddComponentsToBrushes(kBehaviors);

    const std::vector<PropertyNode> rows = Inspector->Nodes();
    const PropertyNode* group = Find(rows, "responses");
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->Descriptor.Label, "Behaviors") << "solid geometry's responses were presented as a trigger";
    EXPECT_EQ(Find(rows, "responses:b.enter"), nullptr) << "an entity's behaviors offered a sensor response";
    EXPECT_EQ(Find(rows, "responses:problems"), nullptr) << "behaviors were asked to sense";
    const PropertyNode* list = Find(rows, "responses:bindings");
    ASSERT_NE(list, nullptr);
    for (const std::string& choice : list->Descriptor.Verbs.front().Choices)
        EXPECT_FALSE(choice.starts_with("sensor.")) << choice;
    EXPECT_EQ(Inspector->Act("responses:bindings", "add", "sensor.enter"), PropertyEditResult::Refused);
}

TEST_F(ResponsesAuthoringTest, TheConfiguredTriggerSavesAndLoads)
{
    const EntityId brush = AddBrush({ 0, 0.5, 0 }, { 1, 0.5, 1 });
    SelectEntity(brush);
    Workspace.Actions.AddComponentsToBrushes(kTriggerVolume);
    ASSERT_EQ(Inspector->Act("responses:b.enter.s.commands", "add", "movement.launch"), PropertyEditResult::Applied);
    ASSERT_EQ(Set("responses:b.enter.s.k0.arg.speed.value", "9"), PropertyEditResult::Applied);
    ASSERT_EQ(Set("responses:b.enter.cooldown", "2"), PropertyEditResult::Applied);
    const std::string configured = Sequence(brush);

    const std::string text = Workspace.ActiveDocument().ToSceneText();
    EditorDocument loaded(Logging);
    ASSERT_TRUE(loaded.LoadFromSceneText(text));
    const ::World& world = loaded.GetRegistry().Components;
    int triggers = 0;
    for (const EntityId entity : loaded.GetScene().GetAllEntities())
        if (const Responses* responses = world.TryGet<Responses>(entity))
        {
            ++triggers;
            const std::span<const ResponseBindingDesc> bindings =
                world.TryGetResource<ResponseDefinitions>()->Sets.Bindings(responses->Set);
            ASSERT_EQ(bindings.size(), 1u);
            EXPECT_FLOAT_EQ(bindings[0].CooldownSeconds, 2.0f);
            EXPECT_TRUE(world.HasComponent<VolumeSensor>(entity));
            EXPECT_TRUE(world.HasComponent<BrushVolume>(entity));
            const JsonValue* sequence = ResponseBindingsSource(world, entity)->Find("enter")->Find("sequence");
            EXPECT_EQ(JsonStringify(*sequence), configured);
        }
    EXPECT_EQ(triggers, 1);
}

TEST_F(ResponsesAuthoringTest, UndoingAPolicyEditAcrossASequenceEditKeepsTheSequence)
{
    const EntityId brush = AddBrush({ 0, 0.5, 0 }, { 1, 0.5, 1 });
    SelectEntity(brush);
    Workspace.Actions.AddComponentsToBrushes(kTriggerVolume);
    ASSERT_EQ(Inspector->Act("responses:b.enter.s.commands", "add", "movement.launch"), PropertyEditResult::Applied);
    const std::string withLaunch = Sequence(brush);
    ASSERT_EQ(Set("responses:b.enter.cooldown", "2"), PropertyEditResult::Applied);
    ASSERT_EQ(Set("responses:b.enter.s.k0.arg.speed.value", "9"), PropertyEditResult::Applied);
    const std::string configured = Sequence(brush);

    Workspace.Commands.Undo();
    EXPECT_EQ(Sequence(brush), withLaunch);
    Workspace.Commands.Undo();
    EXPECT_EQ(Sequence(brush), withLaunch) << "undoing the cooldown restored a sequence that no longer resolves";
    EXPECT_NE(Workspace.ActiveDocument().ToSceneText().find("movement.launch"), std::string::npos)
        << "the level saved without the sequence";

    Workspace.Commands.Redo();
    Workspace.Commands.Redo();
    EXPECT_EQ(Sequence(brush), configured);
}
