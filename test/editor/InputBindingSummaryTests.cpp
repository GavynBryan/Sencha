#include <gtest/gtest.h>

#include "input/InputBindingSummary.h"

#include <core/json/JsonParser.h>

#include <string>

// The wording here is what a collapsed card shows, so it is pinned: a card that
// reads as a keymap is the whole point of the surface, and a phrase change is a
// test update rather than an accident.

namespace
{
JsonValue Parse(std::string_view json)
{
    std::optional<JsonValue> value = JsonParse(json);
    EXPECT_TRUE(value.has_value());
    return value.value_or(JsonValue{});
}
}

// ---------------------------------------------------------------------------
// Control names
// ---------------------------------------------------------------------------

TEST(DescribeInputControl, ReadsKeysAsTheirLabels)
{
    EXPECT_EQ(DescribeInputControlName("key.a"), "A");
    EXPECT_EQ(DescribeInputControlName("key.space"), "Space");
    EXPECT_EQ(DescribeInputControlName("key.left_shift"), "Left Shift");
}

TEST(DescribeInputControl, NamesMouseControlsAsAPlayerWould)
{
    EXPECT_EQ(DescribeInputControlName("mouse.left"), "Left mouse button");
    EXPECT_EQ(DescribeInputControlName("mouse.delta"), "Mouse movement");
    EXPECT_EQ(DescribeInputControlName("mouse.wheel"), "Mouse wheel");
}

TEST(DescribeInputControl, NamesGamepadControlsByPosition)
{
    EXPECT_EQ(DescribeInputControlName("gamepad.south"), "Gamepad South");
    EXPECT_EQ(DescribeInputControlName("gamepad.left_stick"), "Gamepad Left Stick");
    EXPECT_EQ(DescribeInputControlName("gamepad.dpad_up"), "Gamepad D-pad Up");
}

TEST(DescribeInputControl, EchoesAnUnrecognizedNameUnchanged)
{
    // The author must see exactly what is authored; prettifying a typo would
    // hide the reason the binding does not work.
    EXPECT_EQ(DescribeInputControlName("key.nonsense"), "Nonsense");
    EXPECT_EQ(DescribeInputControlName("joystick.x"), "joystick.x");
    EXPECT_EQ(DescribeInputControlName("bare"), "bare");
}

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

TEST(DescribeInputBinding, RecognizesTheArrangementsAuthorsKnowOnSight)
{
    EXPECT_EQ(DescribeInputBinding(Parse(R"({ "action": "move", "composite": "cardinal",
        "left": "key.a", "right": "key.d", "down": "key.s", "up": "key.w" })")), "WASD");

    EXPECT_EQ(DescribeInputBinding(Parse(R"({ "action": "move", "composite": "cardinal",
        "left": "key.left", "right": "key.right", "down": "key.down", "up": "key.up" })")),
        "Arrow keys");
}

TEST(DescribeInputBinding, SpellsOutAnUnfamiliarComposite)
{
    EXPECT_EQ(DescribeInputBinding(Parse(R"({ "composite": "cardinal",
        "left": "key.j", "right": "key.l", "down": "key.k", "up": "key.i" })")),
        "J / L / K / I");

    EXPECT_EQ(DescribeInputBinding(Parse(R"({ "composite": "axis",
        "negative": "key.q", "positive": "key.e" })")), "Q / E");
}

TEST(DescribeInputBinding, SaysWhenACompositeIsIncomplete)
{
    // Half-filled is a normal intermediate state while authoring; it should read
    // as unfinished rather than as a binding that will work.
    EXPECT_EQ(DescribeInputBinding(Parse(R"({ "composite": "cardinal",
        "left": "key.a", "right": "key.d", "up": "key.w" })")), "four directions, not finished");
    EXPECT_EQ(DescribeInputBinding(Parse(R"({ "composite": "axis", "negative": "key.q" })")),
              "two buttons, not finished");
}

TEST(DescribeInputBinding, NamesASingleControlAndItsConditioning)
{
    EXPECT_EQ(DescribeInputBinding(Parse(R"({ "control": "gamepad.south" })")), "Gamepad South");
    EXPECT_EQ(DescribeInputBinding(Parse(R"({ "control": "mouse.delta", "scale": 0.0025 })")),
              "Mouse movement, scale 0.0025");
    EXPECT_EQ(DescribeInputBinding(Parse(
        R"({ "control": "gamepad.left_stick", "dead_zone": 0.2, "invert_y": true })")),
        "Gamepad Left Stick, dead zone 0.2, inverted");
}

TEST(DescribeInputBinding, LeavesNeutralConditioningUnsaid)
{
    // Scale one and dead zone zero change nothing, so naming them is noise.
    EXPECT_EQ(DescribeInputBinding(Parse(
        R"({ "control": "key.space", "scale": 1.0, "dead_zone": 0.0, "invert_y": false })")),
        "Space");
}

TEST(DescribeInputBinding, SaysWhenNothingIsBoundYet)
{
    EXPECT_EQ(DescribeInputBinding(Parse(R"({ "action": "jump" })")), "not bound yet");
}

TEST(InputCardTitles, ReadAsAKeymapLine)
{
    EXPECT_EQ(InputBindingCardTitle(Parse(R"({ "action": "jump", "control": "key.space" })")),
              "jump - Space");
    EXPECT_EQ(InputBindingCardTitle(Parse(R"({ "control": "key.space" })")), "Binding");
}

TEST(InputCardTitles, SummarizeAContextByWhatItHolds)
{
    EXPECT_EQ(InputContextCardSubtitle(Parse(
        R"({ "name": "gameplay", "priority": 100, "bindings": [{}, {}, {}] })")),
        "priority 100 - 3 bindings");
    EXPECT_EQ(InputContextCardSubtitle(Parse(R"({ "priority": 10, "bindings": [{}] })")),
              "priority 10 - 1 binding");
    EXPECT_EQ(InputContextCardSubtitle(Parse(R"({ "priority": 0, "bindings": [] })")),
              "priority 0 - 0 bindings");
}

// ---------------------------------------------------------------------------
// Vocabulary collection
// ---------------------------------------------------------------------------

TEST(CollectInputActions, ReadsTheDeclaredVocabulary)
{
    const std::vector<KnownInputAction> actions = CollectInputActions(Parse(R"({
        "actions": [
            { "name": "move", "type": "axis2" },
            { "name": "throttle", "type": "axis1" },
            { "name": "jump", "type": "digital" },
            { "name": "pause", "type": "digital", "scope": "presentation" }
        ] })"));

    ASSERT_EQ(actions.size(), 4u);
    EXPECT_EQ(actions[0].Name, "move");
    EXPECT_EQ(actions[0].Type, InputActionType::Axis2D);
    EXPECT_EQ(actions[1].Type, InputActionType::Axis1D);
    EXPECT_EQ(actions[2].Type, InputActionType::Digital);
    EXPECT_EQ(actions[0].Scope, InputActionScope::Simulation);
    EXPECT_EQ(actions[3].Scope, InputActionScope::Presentation);
}

TEST(CollectInputActions, SkipsMalformedEntriesRatherThanGivingUp)
{
    // The vocabulary is edited in the same session as the profile reading it, so
    // a half-typed action must not blank the picker.
    const std::vector<KnownInputAction> actions = CollectInputActions(Parse(R"({
        "actions": [ { "name": "move" }, { "type": "digital" }, 7, { "name": "jump" } ] })"));

    ASSERT_EQ(actions.size(), 2u);
    EXPECT_EQ(actions[0].Name, "move");
    EXPECT_EQ(actions[1].Name, "jump");
}

TEST(CollectInputActions, ReportsNothingForADocumentWithNoActions)
{
    EXPECT_TRUE(CollectInputActions(Parse(R"({})")).empty());
}

TEST(CollectBoundActions, ListsEachActionOnceInAppearanceOrder)
{
    const JsonValue profile = Parse(R"({ "contexts": [
        { "bindings": [ { "action": "move" }, { "action": "jump" } ] },
        { "bindings": [ { "action": "jump" }, { "action": "pause" } ] } ] })");

    const std::vector<std::string> bound = CollectBoundActionNames(profile);
    ASSERT_EQ(bound.size(), 3u);
    EXPECT_EQ(bound[0], "move");
    EXPECT_EQ(bound[1], "jump");
    EXPECT_EQ(bound[2], "pause");
}

TEST(UnboundInputActions, NamesWhatIsDeclaredButNeverBound)
{
    const std::vector<KnownInputAction> declared = CollectInputActions(Parse(R"({
        "actions": [ { "name": "move" }, { "name": "jump" }, { "name": "interact" } ] })"));
    const JsonValue profile = Parse(R"({ "contexts": [
        { "bindings": [ { "action": "move" } ] } ] })");

    const std::vector<std::string> unbound = UnboundInputActions(declared, profile);
    ASSERT_EQ(unbound.size(), 2u);
    EXPECT_EQ(unbound[0], "jump");
    EXPECT_EQ(unbound[1], "interact");
}

TEST(FormatInputScalar, TrimsTrailingZeros)
{
    EXPECT_EQ(FormatInputScalar(1.0), "1");
    EXPECT_EQ(FormatInputScalar(0.0025), "0.0025");
    EXPECT_EQ(FormatInputScalar(0.2), "0.2");
    EXPECT_EQ(FormatInputScalar(100.0), "100");
}
