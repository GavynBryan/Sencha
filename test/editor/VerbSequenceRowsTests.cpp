#include "vocabulary/VerbSequenceRows.h"

#include <app/EngineVocabulary.h>
#include <authored/WorldVocabulary.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <ecs/World.h>
#include <logic/TriggerSequences.h>

#include <gtest/gtest.h>

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
        Vocabulary = VerbSequenceVocabulary{ .Verbs = FindVerbRegistry(Names),
                                             .Queries = FindAuthoredQueryRegistry(Names),
                                             .Ports = kTriggerPorts };
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
    VerbSequenceVocabulary Vocabulary;
};
} // namespace

TEST_F(VerbSequenceRowsTest, ALaunchIsAddedAimedAtTheActivatorAndGivenADirectionInTheTriggersFrame)
{
    JsonValue sequence = EmptyVerbSequence();
    const std::vector<PropertyNode> empty = VerbSequenceRows(sequence, Vocabulary, {});
    const PropertyNode* commands = Find(empty, "commands");
    ASSERT_NE(commands, nullptr);
    const auto& offered = commands->Descriptor.Verbs.front().Choices;
    EXPECT_NE(std::ranges::find(offered, "movement.launch"), offered.end());
    EXPECT_EQ(std::ranges::find(offered, "movement.burst"), offered.end()) << "a lease was offered as a command";

    sequence = Must(ActOnVerbSequence(sequence, Vocabulary, "commands", "add", "movement.launch"));
    // The target starts at the activator, the one port that can be a character.
    EXPECT_EQ(JsonStringify(*sequence.Find("commands")->AsArray()[0].Find("arguments")->Find("target")),
              R"({"port":"activator"})");

    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "k0.arg.direction.value", "0, 0, 1"));
    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "k0.arg.direction.frame", "trigger"));
    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "k0.arg.speed.value", "14"));
    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "k0.arg.direction.value", "1, 0, 0"));
    const JsonValue& direction = *sequence.Find("commands")->AsArray()[0].Find("arguments")->Find("direction");
    EXPECT_EQ(JsonStringify(direction), R"({"const":[1,0,0],"frame":"trigger"})")
        << "retyping the direction dropped the frame it is given in";

    const std::vector<PropertyNode> rows = VerbSequenceRows(sequence, Vocabulary, {});
    const PropertyNode* frame = Find(rows, "k0.arg.direction.frame");
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->Value, "trigger");
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
    sequence = Must(ActOnVerbSequence(sequence, Vocabulary, "commands", "add", "trigger.disable"));
    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "k1.on_failure", "Stop"));
    sequence = Must(ActOnVerbSequence(sequence, Vocabulary, "k1", "up", ""));
    const auto& commands = sequence.Find("commands")->AsArray();
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[0].Find("verb")->AsString(), "trigger.disable");
    EXPECT_EQ(commands[0].Find("on_failure")->AsString(), "stop");

    // trigger.disable's argument is the trigger; switching to launch keeps no
    // argument launch does not take, and starts the ones it requires.
    sequence = Must(SetVerbSequenceValue(sequence, Vocabulary, "k0.verb", "movement.launch"));
    const JsonValue& arguments = *sequence.Find("commands")->AsArray()[0].Find("arguments");
    EXPECT_EQ(arguments.Find("trigger"), nullptr);
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
