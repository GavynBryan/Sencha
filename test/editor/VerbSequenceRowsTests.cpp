#include "vocabulary/VerbSequenceRows.h"

#include <app/EngineVocabulary.h>
#include <authored/WorldVocabulary.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <ecs/World.h>
#include <logic/ResponseSet.h>
#include <logic/ResponsesSystem.h>

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>

// A sequence edited as properties: what the rows show comes from the verbs'
// contracts and the producer's ports, and every edit is a new sequence that
// keeps whatever it did not touch.

namespace
{
class VerbSequenceRowsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        InstallAuthoredVocabulary(Names);
        ASSERT_TRUE(InstallEngineVocabulary(Names));
        // A sensor volume's: the entity, then its activator.
        VerbSequenceStore::PortsFor(kResponsePorts, Names, kSensorEnterEvent, PortNames, Ports);
        Vocabulary = VerbSequenceVocabulary{ .Verbs = FindVerbRegistry(Names),
                                             .Queries = FindAuthoredQueryRegistry(Names),
                                             .Ports = Ports,
                                             .StartingPort = "activator" };
    }

    JsonValue Must(std::optional<JsonValue> edited, std::source_location where = std::source_location::current())
    {
        EXPECT_TRUE(edited.has_value()) << "refused at line " << where.line();
        return edited.value_or(JsonValue());
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

    World Names;
    std::vector<std::string> PortNames;
    std::vector<SequencePort> Ports;
    VerbSequenceVocabulary Vocabulary;
};
} // namespace

TEST_F(VerbSequenceRowsTest, ALaunchIsAddedAimedAtTheActivatorAndGivenADirectionInTheVolumesFrame)
{
    JsonValue sequence = EmptyVerbSequence();
    const std::vector<PropertyNode> empty = VerbSequenceRows(sequence, Vocabulary, {});
    const PropertyNode* commands = Find(empty, "commands");
    ASSERT_NE(commands, nullptr);
    const auto& offered = commands->Descriptor.Verbs.front().Choices;
    EXPECT_NE(std::ranges::find(offered, "movement.launch"), offered.end());
    EXPECT_EQ(std::ranges::find(offered, "movement.burst"), offered.end()) << "a lease was offered as a command";

    sequence = Must(ActOnVerbSequence(sequence, Vocabulary, "commands", "add", "movement.launch"));
    // The target starts at the activator, whoever caused the occurrence.
    EXPECT_EQ(JsonStringify(*sequence.Find("commands")->AsArray()[0].Find("arguments")->Find("target")),
              R"({"port":"activator"})");
    EXPECT_EQ(JsonStringify(*sequence.Find("commands")->AsArray()[0].Find("arguments")->Find("direction")),
              R"({"const":[0,0,1]})") << "a launch started with no way to go";

    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "k0.arg.direction.value", "0, 0, 1"));
    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "k0.arg.direction.frame", "self"));
    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "k0.arg.speed.value", "14"));
    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "k0.arg.direction.value", "1, 0, 0"));
    const JsonValue& direction = *sequence.Find("commands")->AsArray()[0].Find("arguments")->Find("direction");
    EXPECT_EQ(JsonStringify(direction), R"({"const":[1,0,0],"frame":"self"})")
        << "retyping the direction dropped the frame it is given in";

    const std::vector<PropertyNode> rows = VerbSequenceRows(sequence, Vocabulary, {});
    const PropertyNode* frame = Find(rows, "k0.arg.direction.frame");
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->Value, "self");
    EXPECT_EQ(Find(rows, "k0.arg.speed.value")->Value, "14");
}

TEST_F(VerbSequenceRowsTest, AValueTheArgumentCannotTakeIsRefused)
{
    JsonValue sequence = Must(ActOnVerbSequence(EmptyVerbSequence(), Vocabulary, "commands", "add", "movement.launch"));
    EXPECT_FALSE(SetVerbSequenceValue(sequence, Vocabulary, "k0.arg.speed.value", "fast").has_value());
    EXPECT_FALSE(SetVerbSequenceValue(sequence, Vocabulary, "k0.arg.target.value", "nowhere").has_value());
    EXPECT_FALSE(SetVerbSequenceValue(sequence, Vocabulary, "k0.verb", "movement.burst.typo").has_value());
    EXPECT_FALSE(SetVerbSequenceValue(sequence, Vocabulary, "k3.verb", "movement.launch").has_value());
}

TEST_F(VerbSequenceRowsTest, CommandsReorderAndAChangedVerbKeepsWhatItStillTakes)
{
    JsonValue sequence = EmptyVerbSequence();
    sequence = Must(ActOnVerbSequence(sequence, Vocabulary, "commands", "add", "movement.launch"));
    sequence = Must(ActOnVerbSequence(sequence, Vocabulary, "commands", "add", "responses.disable"));
    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "k1.on_failure", "Stop"));
    sequence = Must(ActOnVerbSequence(sequence, Vocabulary, "k1", "up", ""));
    const auto& commands = sequence.Find("commands")->AsArray();
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[0].Find("verb")->AsString(), "responses.disable");
    EXPECT_EQ(commands[0].Find("on_failure")->AsString(), "stop");

    // responses.disable's arguments are the entity and a binding; switching to
    // launch keeps no argument launch does not take, and starts the ones it requires.
    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "k0.verb", "movement.launch"));
    const JsonValue& arguments = *sequence.Find("commands")->AsArray()[0].Find("arguments");
    EXPECT_EQ(arguments.Find("entity"), nullptr);
    EXPECT_NE(arguments.Find("target"), nullptr);

    sequence = Must(ActOnVerbSequence(sequence, Vocabulary, "k1", "remove", ""));
    EXPECT_EQ(sequence.Find("commands")->AsArray().size(), 1u);
}

TEST_F(VerbSequenceRowsTest, AnActivatorFilterIsAnEntityCondition)
{
    JsonValue sequence = Must(ActOnVerbSequence(EmptyVerbSequence(), Vocabulary, "conditions", "add",
                                                "Entity has\xE2\x80\xA6"));
    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "c0.has", "sencha.character_movement, sencha.ability_grants"));
    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "c0.tags.none", "State.Dead"));
    EXPECT_EQ(JsonStringify(sequence.Find("conditions")->AsArray()[0]),
              R"({"entity":"activator","has":["sencha.character_movement","sencha.ability_grants"],"tags":{"none":["State.Dead"]}})");
}

TEST_F(VerbSequenceRowsTest, WhatTheRowsDoNotTouchIsKept)
{
    const std::optional<JsonValue> authored = JsonParse(R"({"commands": [
        {"verb": "game.not_here", "arguments": {"anything": {"const": 3}}, "note": "kept"}], "conditions": []})");
    ASSERT_TRUE(authored.has_value());
    const std::vector<std::string> problems{ "a problem" };
    const std::vector<PropertyNode> rows = VerbSequenceRows(*authored, Vocabulary, problems);
    EXPECT_NE(Find(rows, "k0.verb"), nullptr) << "an undeclared verb's command was hidden";
    EXPECT_NE(Find(rows, "problem0"), nullptr);
    const JsonValue moved = Must(SetVerbSequenceValue(*authored, Vocabulary, "k0.on_failure", "Stop"));
    EXPECT_NE(moved.Find("commands")->AsArray()[0].Find("note"), nullptr);
    EXPECT_NE(moved.Find("commands")->AsArray()[0].Find("arguments")->Find("anything"), nullptr);
}

TEST_F(VerbSequenceRowsTest, AnOccurrencesTagIsConditionedOnWhereItHasATagPort)
{
    const std::vector<PropertyNode> plain = VerbSequenceRows(EmptyVerbSequence(), Vocabulary, {});
    const auto& choices = Find(plain, "conditions")->Descriptor.Verbs.front().Choices;
    EXPECT_EQ(std::ranges::find(choices, "Tag is\xE2\x80\xA6"), choices.end())
        << "a tag condition was offered where nothing carries a tag";

    // A struck entity's: itself, then what struck it and with what.
    const std::array<SequencePort, 3> struck = { {
        { "self", DataFieldKind::Entity },
        { "instigator", DataFieldKind::Entity },
        { "attack", DataFieldKind::GameplayTag },
    } };
    const VerbSequenceVocabulary vocabulary{
        .Verbs = Vocabulary.Verbs, .Queries = Vocabulary.Queries, .Ports = struck, .StartingPort = "instigator" };
    JsonValue sequence = Must(ActOnVerbSequence(EmptyVerbSequence(), vocabulary, "conditions", "add", "Tag is\xE2\x80\xA6"));
    sequence = Must(SetVerbSequenceValue(sequence, vocabulary, "c0.tags.any", "Attack.Charge, Attack.Rail"));
    EXPECT_EQ(JsonStringify(sequence.Find("conditions")->AsArray()[0]),
              R"({"tag":"attack","tags":{"any":["Attack.Charge","Attack.Rail"]}})");
    EXPECT_FALSE(SetVerbSequenceValue(sequence, vocabulary, "c0.tag", "instigator").has_value())
        << "an entity port was accepted as a tag";

    const std::vector<PropertyNode> rows = VerbSequenceRows(sequence, vocabulary, {});
    ASSERT_NE(Find(rows, "c0.tag"), nullptr);
    EXPECT_EQ(Find(rows, "c0.tag")->Value, "attack");
}
