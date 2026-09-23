// Clip events: bound against the rig's authored bindings, and produced as
// invocations of the verbs those bindings name.

#include "AnimRigFixture.h"

#include <authored/VerbRegistry.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    DataFieldSchema Field(std::string key, DataFieldKind kind)
    {
        DataFieldSchema field;
        field.Key = std::move(key);
        field.Kind = kind;
        return field;
    }

    bool Declare(VerbRegistry& registry, std::string name, std::vector<DataFieldSchema> arguments)
    {
        VerbDefinition definition;
        definition.Name = std::move(name);
        definition.Arguments.Children = std::move(arguments);
        VerbRegistrationScope scope(registry, "test");
        (void)scope.Declare(std::move(definition));
        return scope.Commit();
    }

    VerbBindingArgument Tag(std::string key, std::string tag)
    {
        VerbBindingArgument argument;
        argument.Key = std::move(key);
        argument.Source = VerbArgumentSource::Tag;
        argument.Text = std::move(tag);
        return argument;
    }

    VerbBindingArgument Const(std::string key, JsonValue value)
    {
        VerbBindingArgument argument;
        argument.Key = std::move(key);
        argument.Literal = std::move(value);
        return argument;
    }

    AnimationClipEvent Event(std::uint32_t key, float time, std::string binding,
                             std::vector<VerbBindingArgument> inputs = {},
                             AnimEventScope scope = AnimEventScope::Cosmetic)
    {
        AnimationClipEvent event;
        event.Key = key;
        event.Time = time;
        event.Binding = std::move(binding);
        event.Inputs = std::move(inputs);
        event.Scope = scope;
        return event;
    }

    // One volume input feeds two float arguments, which is the case a
    // per-destination check exists for.
    constexpr std::string_view kBindings = R"({ "bindings": [
        { "key": "anim.footstep", "verb": "test.footstep", "inputs": [ "surface", "volume" ],
          "arguments": { "Surface": { "input": "surface" }, "Volume": { "input": "volume" },
                         "Gain": { "input": "volume" } } },
        { "key": "anim.count", "verb": "test.count", "inputs": [ "amount" ],
          "arguments": { "Amount": { "input": "amount" }, "Scale": { "input": "amount" } } } ] })";

    constexpr std::string_view kSlots = R"({ "rows": [
        { "behavior": "Anim.Walk", "clip": "asset://anim/walk.sanim" } ] })";

    constexpr std::string_view kBehaviors = R"({ "behaviors": [
        { "tag": "Anim.Walk", "kind": "cyclic", "event_weight": 0.5 } ] })";

    constexpr std::string_view kRig = R"({
        "behaviors": [ "asset://anim/ev.behaviors.sdata" ],
        "slot_maps": [ "asset://anim/ev.slots.sdata" ],
        "bindings": [ "asset://anim/ev.bindings.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Walk" } ] })";

    void DeclareVerbs(AnimRigFixture& fx)
    {
        ASSERT_TRUE(Declare(fx.Verbs(), "test.footstep",
                            { Field("Surface", DataFieldKind::GameplayTag), Field("Volume", DataFieldKind::Float),
                              Field("Gain", DataFieldKind::Float) }));
        ASSERT_TRUE(Declare(fx.Verbs(), "test.count",
                            { Field("Amount", DataFieldKind::Int), Field("Scale", DataFieldKind::Float) }));
    }

    DataAssetHandle LoadRig(AnimRigFixture& fx)
    {
        (void)fx.Load("asset://anim/ev.bindings.sdata", kVerbBindingsTypeName, kBindings);
        (void)fx.Load("asset://anim/ev.behaviors.sdata", kAnimBehaviorSetType, kBehaviors);
        (void)fx.Load("asset://anim/ev.slots.sdata", kAnimSlotMapType, kSlots);
        return fx.Load("asset://anim/ev.rig.sdata", kAnimRigType, kRig);
    }
}

TEST(AnimEventBinding, EventsBindToTheRigsBindingsWithTheirInputsConverted)
{
    AnimRigFixture fx({ "Anim.Walk", "Surface.Grass" });
    DeclareVerbs(fx);
    fx.Clip("asset://anim/walk.sanim", 1.0f,
            { Event(5, 0.25f, "anim.footstep", { Tag("surface", "Surface.Grass"), Const("volume", JsonValue(0.5)) }) });
    const AnimBoundRig& bound = fx.Bound(LoadRig(fx));
    ASSERT_TRUE(bound.Valid) << AnimRigFixture::Describe(bound);
    EXPECT_TRUE(bound.Diagnostics.empty()) << AnimRigFixture::Describe(bound);

    ASSERT_EQ(bound.Contents.size(), 1u);
    ASSERT_EQ(bound.Contents[0].Events.size(), 1u);
    const AnimBoundEvent& event = bound.Contents[0].Events[0];
    EXPECT_TRUE(event.Resolved);
    EXPECT_EQ(event.Key, 5u);
    EXPECT_EQ(event.Binding, MakeVerbBindingKey("anim.footstep"));
    ASSERT_EQ(event.Inputs.size(), 2u);
    GameplayTagId surface;
    ASSERT_TRUE(event.Inputs[0].TryGetTag(surface));
    EXPECT_EQ(surface, fx.Tag("Surface.Grass"));
    double volume = 0.0;
    ASSERT_TRUE(event.Inputs[1].TryGetFloat(volume));
    EXPECT_DOUBLE_EQ(volume, 0.5);
}

// A broken event is the event's problem: it is reported where it was
// authored and left unresolved, and the rig still animates.
TEST(AnimEventBinding, BrokenEventsAreLocatedWarningsThatLeaveTheRigValid)
{
    AnimRigFixture fx({ "Anim.Walk", "Surface.Grass" });
    DeclareVerbs(fx);
    fx.Clip("asset://anim/walk.sanim", 1.0f,
            {
                Event(1, 0.1f, "anim.nope"),
                Event(2, 0.2f, "anim.footstep",
                      { Tag("surface", "Surface.Grass"), Const("volume", JsonValue("loud")) }),
                Event(3, 0.3f, "anim.footstep", { Const("volume", JsonValue(0.5)) }),
                Event(4, 0.4f, "anim.footstep",
                      { Tag("surface", "Surface.Grass"), Const("volume", JsonValue(0.5)),
                        Const("pitch", JsonValue(2.0)) }),
            });
    const AnimBoundRig& bound = fx.Bound(LoadRig(fx));
    EXPECT_TRUE(bound.Valid) << AnimRigFixture::Describe(bound);

    for (const char* code : { "anim.event.binding_unknown", "anim.event.input_invalid", "anim.event.input_missing",
                              "anim.event.input_unused" })
    {
        const AnimDiagnostic* diagnostic = AnimRigFixture::FindCode(bound, code);
        ASSERT_NE(diagnostic, nullptr) << code << "\n" << AnimRigFixture::Describe(bound);
        EXPECT_EQ(diagnostic->Severity, AnimDiagnosticSeverity::Warning);
        EXPECT_EQ(diagnostic->AssetPath, "asset://anim/walk.sanim");
    }
    EXPECT_EQ(AnimRigFixture::FindCode(bound, "anim.event.binding_unknown")->FieldPath, "$.events[0]");
    for (const AnimBoundEvent& event : bound.Contents.at(0).Events)
        EXPECT_FALSE(event.Resolved) << event.Key;
}

// One input filling an Int and a Float argument must suit both; a float
// literal does not suit the Int.
TEST(AnimEventBinding, AnInputIsCheckedAgainstEveryArgumentItFills)
{
    AnimRigFixture fx({ "Anim.Walk" });
    DeclareVerbs(fx);
    fx.Clip("asset://anim/walk.sanim", 1.0f, { Event(1, 0.5f, "anim.count", { Const("amount", JsonValue(2.5)) }) });
    const AnimBoundRig& bound = fx.Bound(LoadRig(fx));
    const AnimDiagnostic* invalid = AnimRigFixture::FindCode(bound, "anim.event.input_invalid");
    ASSERT_NE(invalid, nullptr) << AnimRigFixture::Describe(bound);
    EXPECT_FALSE(bound.Contents.at(0).Events.at(0).Resolved);
}

// Events bound before their verb was declared resolve once it is: the catalog
// is part of what the rig was bound against.
TEST(AnimEventBinding, ALaterDeclarationRebindsTheRig)
{
    AnimRigFixture fx({ "Anim.Walk", "Surface.Grass" });
    fx.Clip("asset://anim/walk.sanim", 1.0f,
            { Event(5, 0.25f, "anim.footstep", { Tag("surface", "Surface.Grass"), Const("volume", JsonValue(0.5)) }) });
    const DataAssetHandle rig = LoadRig(fx);
    EXPECT_FALSE(fx.Bound(rig).Contents.at(0).Events.at(0).Resolved);
    EXPECT_NE(AnimRigFixture::FindCode(fx.Bound(rig), "anim.event.binding_invalid"), nullptr);

    DeclareVerbs(fx);
    const AnimBoundRig& rebound = fx.Bound(rig);
    EXPECT_TRUE(rebound.Contents.at(0).Events.at(0).Resolved) << AnimRigFixture::Describe(rebound);
}

// Content chosen by a local fact must commit the authority to the same
// gameplay events, whichever row a machine picked.
TEST(AnimEventBinding, RowsReadingLocalFactsMustShareGameplayEvents)
{
    AnimRigFixture fx({ "Anim.Idle" });
    fx.Clip("asset://anim/a.sanim", 1.0f, { Event(1, 0.5f, "melee.hit", {}, AnimEventScope::Gameplay) });
    fx.Clip("asset://anim/b.sanim", 1.0f, { Event(1, 0.6f, "melee.hit", {}, AnimEventScope::Gameplay) });
    (void)fx.Load("asset://anim/l.facts.sdata", kAnimFactSchemaType, R"({
        "slots": [ { "name": "Variant", "kind": "int", "local": true } ] })");
    (void)fx.Load("asset://anim/l.slots.sdata", kAnimSlotMapType, R"({ "rows": [
        { "behavior": "Anim.Idle", "when": [ { "fact": "Variant", "compare": "eq", "value": 1 } ],
          "clip": "asset://anim/a.sanim" },
        { "behavior": "Anim.Idle", "clip": "asset://anim/b.sanim" } ] })");
    const DataAssetHandle rig = fx.Load("asset://anim/l.rig.sdata", kAnimRigType, R"({
        "facts": "asset://anim/l.facts.sdata", "slot_maps": [ "asset://anim/l.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" } ] })");

    const AnimBoundRig& bound = fx.Bound(rig);
    const AnimDiagnostic* events = AnimRigFixture::FindCode(bound, "anim.slot.local_events");
    ASSERT_NE(events, nullptr) << AnimRigFixture::Describe(bound);
    EXPECT_EQ(events->FieldPath, "$.data.rows[0].when");
}
