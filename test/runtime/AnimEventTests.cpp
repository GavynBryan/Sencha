// Clip events: bound against the rig's authored bindings, and produced as
// invocations of the verbs those bindings name.

#include "AnimRigFixture.h"

#include <anim/AnimEventSystem.h>
#include <authored/VerbDispatcher.h>
#include <authored/VerbRegistry.h>
#include <world/SimulationAuthority.h>

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

// -- Producing events -------------------------------------------------------------

namespace
{
    // What an implementation behind the footstep verb saw, per call.
    struct FootstepRecorder
    {
        struct Call
        {
            EntityId Producer;
            EntityId Instigator;
            std::uint64_t Tick = 0;
            GameplayTagId Surface;
            double Volume = 0.0;
        };
        std::vector<Call> Calls;

        VerbAdmission Invoke(const VerbInvocation& invocation)
        {
            Call call{ invocation.Producer, invocation.Instigator, invocation.Tick, {}, 0.0 };
            (void)invocation.Arguments->TryGetTag(0, call.Surface);
            (void)invocation.Arguments->TryGetFloat(1, call.Volume);
            Calls.push_back(call);
            return VerbAdmission::Accepted;
        }
    };

    // The pipeline plus the event pass, into a real dispatcher over the
    // fixture's catalog with a recorder bound behind the footstep verb.
    struct EventHarness
    {
        AnimRigFixture Fx{ { "Anim.Walk", "Anim.Door.Open", "Surface.Grass" } };
        VerbDispatcher Dispatcher{ Fx.Verbs() };
        FootstepRecorder Footsteps;
        VerbBindingToken Token;
        AnimEventSystem Events{ &Dispatcher, true };

        EventHarness() { DeclareVerbs(Fx); }

        void BindFootstep() { Token = Dispatcher.Bind(Fx.Verbs().Find("test.footstep"), Footsteps); }

        EntityId Prop(DataAssetHandle rig)
        {
            const EntityId entity = Fx.Entities.CreateEntity();
            Fx.Entities.AddComponent(entity, AnimRig{ rig });
            Fx.Entities.AddComponent(entity, AnimDecisionLog{});
            return entity;
        }

        void Step(int ticks = 1, bool runEvents = true)
        {
            for (int i = 0; i < ticks; ++i)
            {
                Fx.Tick();
                if (runEvents)
                    Events.Run(Fx.Entities, Fx.Last(), AnimRigFixture::kTick);
            }
        }

        std::vector<const AnimDecisionRecord*> EventRecords(EntityId entity)
        {
            std::vector<const AnimDecisionRecord*> records;
            const AnimDecisionLog& log = Fx.Log(entity);
            for (std::size_t i = 0; i < log.Size(); ++i)
                if (log.At(i).Cause == AnimDecisionCause::EventCrossed)
                    records.push_back(&log.At(i));
            return records;
        }
    };

    std::vector<VerbBindingArgument> GrassInputs()
    {
        return { Tag("surface", "Surface.Grass"), Const("volume", JsonValue(0.5)) };
    }
}

// A cyclic clip crosses its mark once per loop, on the tick clock: two loops
// are two invocations exactly one clip length apart.
TEST(AnimEvents, ACyclicMarkFiresOncePerLoop)
{
    EventHarness h;
    h.BindFootstep();
    h.Fx.Clip("asset://anim/walk.sanim", 1.0f, { Event(5, 0.25f, "anim.footstep", GrassInputs()) });
    const EntityId walker = h.Prop(LoadRig(h.Fx));

    h.Step(120);
    ASSERT_EQ(h.Footsteps.Calls.size(), 2u);
    EXPECT_EQ(h.Footsteps.Calls[1].Tick - h.Footsteps.Calls[0].Tick, 60u);
    EXPECT_NEAR(static_cast<double>(h.Footsteps.Calls[0].Tick) * AnimRigFixture::kTick, 0.25, AnimRigFixture::kTick);
    EXPECT_EQ(h.Footsteps.Calls[0].Producer, walker);
    EXPECT_EQ(h.Footsteps.Calls[0].Surface, h.Fx.Tag("Surface.Grass"));
    EXPECT_DOUBLE_EQ(h.Footsteps.Calls[0].Volume, 0.5);

    const std::vector<const AnimDecisionRecord*> records = h.EventRecords(walker);
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0]->EventKey, 5u);
    EXPECT_EQ(records[0]->EventOutcome, AnimEventOutcome::Fired);
    EXPECT_EQ(records[0]->Admission, VerbAdmission::Accepted);
    EXPECT_EQ(records[0]->Tick, h.Footsteps.Calls[0].Tick);
}

// A mark at the start of the clip is crossed on the entry tick, and a
// one-shot that has finished crosses nothing more.
TEST(AnimEvents, AStartMarkFiresOnEntryAndAFinishedOneShotIsQuiet)
{
    EventHarness h;
    h.BindFootstep();
    h.Fx.Clip("asset://anim/walk.sanim", 0.5f,
              { Event(1, 0.0f, "anim.footstep", GrassInputs()), Event(2, 1.0f, "anim.footstep", GrassInputs()) });
    (void)h.Fx.Load("asset://anim/ev.bindings.sdata", kVerbBindingsTypeName, kBindings);
    (void)h.Fx.Load("asset://anim/os.behaviors.sdata", kAnimBehaviorSetType,
                    R"({ "behaviors": [ { "tag": "Anim.Walk", "kind": "one_shot" } ] })");
    (void)h.Fx.Load("asset://anim/ev.slots.sdata", kAnimSlotMapType, kSlots);
    const DataAssetHandle rig = h.Fx.Load("asset://anim/os.rig.sdata", kAnimRigType, R"({
        "behaviors": [ "asset://anim/os.behaviors.sdata" ], "slot_maps": [ "asset://anim/ev.slots.sdata" ],
        "bindings": [ "asset://anim/ev.bindings.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Walk" } ] })");
    (void)h.Prop(rig);

    h.Step();
    ASSERT_EQ(h.Footsteps.Calls.size(), 1u);
    EXPECT_EQ(h.Footsteps.Calls[0].Tick, 0u);
    h.Step(90);
    // The end mark once, at the tick the clip completed; nothing after.
    ASSERT_EQ(h.Footsteps.Calls.size(), 2u);
    EXPECT_NEAR(static_cast<double>(h.Footsteps.Calls[1].Tick) * AnimRigFixture::kTick, 0.5, AnimRigFixture::kTick);
}

// Gameplay events are the authority's to produce, cosmetic ones the
// presenter's; neither gate is logged as a crossing, because on that machine
// the event does not exist.
TEST(AnimEvents, ScopeDecidesWhereAnEventIsProduced)
{
    for (const bool authority : { true, false })
        for (const bool presents : { true, false })
        {
            EventHarness h;
            h.BindFootstep();
            h.Events = AnimEventSystem(&h.Dispatcher, presents);
            h.Fx.Entities.SetResource(SimulationAuthority{ authority });
            h.Fx.Clip("asset://anim/walk.sanim", 1.0f,
                      { Event(1, 0.1f, "anim.footstep", GrassInputs(), AnimEventScope::Cosmetic),
                        Event(2, 0.2f, "anim.footstep", GrassInputs(), AnimEventScope::Gameplay) });
            (void)h.Prop(LoadRig(h.Fx));
            h.Step(30);

            const std::size_t expected = (presents ? 1u : 0u) + (authority ? 1u : 0u);
            EXPECT_EQ(h.Footsteps.Calls.size(), expected) << "authority " << authority << " presents " << presents;
        }
}

// Under its threshold a cosmetic event is suppressed and says so; an event
// with its own lower threshold still fires.
TEST(AnimEvents, ACosmeticEventUnderItsWeightIsSuppressed)
{
    EventHarness h;
    h.BindFootstep();
    AnimationClipEvent quiet = Event(1, 0.1f, "anim.footstep", GrassInputs());
    AnimationClipEvent loud = Event(2, 0.2f, "anim.footstep", GrassInputs());
    loud.MinWeight = 0.2f;
    h.Fx.Clip("asset://anim/walk.sanim", 1.0f, { quiet, loud });
    (void)h.Fx.Load("asset://anim/ev.bindings.sdata", kVerbBindingsTypeName, kBindings);
    (void)h.Fx.Load("asset://anim/ev.behaviors.sdata", kAnimBehaviorSetType, kBehaviors);
    (void)h.Fx.Load("asset://anim/ev.slots.sdata", kAnimSlotMapType, kSlots);
    const DataAssetHandle rig = h.Fx.Load("asset://anim/light.rig.sdata", kAnimRigType, R"({
        "behaviors": [ "asset://anim/ev.behaviors.sdata" ], "slot_maps": [ "asset://anim/ev.slots.sdata" ],
        "bindings": [ "asset://anim/ev.bindings.sdata" ],
        "layers": [ { "name": "anim.layer.base", "weight": 0.3, "idle": "Anim.Walk" } ] })");
    const EntityId walker = h.Prop(rig);
    h.Step(30);

    ASSERT_EQ(h.Footsteps.Calls.size(), 1u);
    const std::vector<const AnimDecisionRecord*> records = h.EventRecords(walker);
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0]->EventKey, 1u);
    EXPECT_EQ(records[0]->EventOutcome, AnimEventOutcome::BelowWeight);
    EXPECT_EQ(records[1]->EventKey, 2u);
    EXPECT_EQ(records[1]->EventOutcome, AnimEventOutcome::Fired);
}

// What a crossing's binding answered is on the record, whether that is an
// unknown binding, a verb nothing implements, or acceptance.
TEST(AnimEvents, EveryAdmissionIsRecorded)
{
    EventHarness h;
    h.Fx.Clip("asset://anim/walk.sanim", 1.0f,
              { Event(1, 0.1f, "anim.nope"), Event(2, 0.2f, "anim.footstep", GrassInputs()) });
    const EntityId walker = h.Prop(LoadRig(h.Fx));

    h.Step(20);
    h.BindFootstep();
    h.Step(60);

    const std::vector<const AnimDecisionRecord*> records = h.EventRecords(walker);
    ASSERT_EQ(records.size(), 4u);
    EXPECT_EQ(records[0]->Admission, VerbAdmission::UnresolvedBinding);
    EXPECT_EQ(records[1]->Admission, VerbAdmission::Unavailable);
    EXPECT_EQ(records[2]->Admission, VerbAdmission::UnresolvedBinding);
    EXPECT_EQ(records[3]->Admission, VerbAdmission::Accepted);
}

// Ticks the event pass did not see were not played: their marks are recorded
// as skipped and produce nothing, except a gameplay mark on the authority,
// which is never the machine catching up.
TEST(AnimEvents, MarksInSkippedTicksAreSkippedExceptGameplayOnTheAuthority)
{
    EventHarness h;
    h.BindFootstep();
    h.Fx.Clip("asset://anim/walk.sanim", 1.0f,
              { Event(1, 0.25f, "anim.footstep", GrassInputs(), AnimEventScope::Cosmetic),
                Event(2, 0.30f, "anim.footstep", GrassInputs(), AnimEventScope::Gameplay) });
    const EntityId walker = h.Prop(LoadRig(h.Fx));

    h.Step(5);
    h.Step(30, false);
    h.Step(5);

    ASSERT_EQ(h.Footsteps.Calls.size(), 1u);
    const std::vector<const AnimDecisionRecord*> records = h.EventRecords(walker);
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0]->EventKey, 1u);
    EXPECT_EQ(records[0]->EventOutcome, AnimEventOutcome::Skipped);
    EXPECT_EQ(records[1]->EventKey, 2u);
    EXPECT_EQ(records[1]->EventOutcome, AnimEventOutcome::Fired);
}

// For request-driven content the request's source caused it, and is the
// invocation's instigator; the animated entity is only its producer.
TEST(AnimEvents, ARequestsSourceIsTheInstigator)
{
    EventHarness h;
    h.BindFootstep();
    h.Fx.Clip("asset://anim/door.sanim", 1.0f, { Event(1, 0.1f, "anim.footstep", GrassInputs()) });
    (void)h.Fx.Load("asset://anim/ev.bindings.sdata", kVerbBindingsTypeName, kBindings);
    (void)h.Fx.Load("asset://anim/door.requests.sdata", kAnimRequestSchemaType,
                    R"({ "intents": [ { "intent": "Anim.Door.Open", "params": [] } ] })");
    (void)h.Fx.Load("asset://anim/door.behaviors.sdata", kAnimBehaviorSetType,
                    R"({ "behaviors": [ { "tag": "Anim.Door.Open", "kind": "one_shot" } ] })");
    (void)h.Fx.Load("asset://anim/door.slots.sdata", kAnimSlotMapType,
                    R"({ "rows": [ { "behavior": "Anim.Door.Open", "clip": "asset://anim/door.sanim" } ] })");
    const DataAssetHandle rig = h.Fx.Load("asset://anim/door.rig.sdata", kAnimRigType, R"({
        "requests": "asset://anim/door.requests.sdata", "behaviors": [ "asset://anim/door.behaviors.sdata" ],
        "slot_maps": [ "asset://anim/door.slots.sdata" ], "bindings": [ "asset://anim/ev.bindings.sdata" ],
        "layers": [ { "name": "anim.layer.base" } ] })");
    const EntityId door = h.Prop(rig);
    const EntityId player = h.Fx.Entities.CreateEntity();

    AnimRequestDesc desc;
    desc.Source = player;
    desc.Intent = h.Fx.Tag("Anim.Door.Open");
    ASSERT_TRUE(IssueAnimRequest(h.Fx.Entities, door, desc, h.Fx.Now).Accepted());
    h.Step(20);

    ASSERT_EQ(h.Footsteps.Calls.size(), 1u);
    EXPECT_EQ(h.Footsteps.Calls[0].Producer, door);
    EXPECT_EQ(h.Footsteps.Calls[0].Instigator, player);
}

// One tick's crossings are bounded; the rest are refused on the record, not
// dropped unseen.
TEST(AnimEvents, CrossingsPastCapacityAreRecordedAsQueueFull)
{
    EventHarness h;
    h.BindFootstep();
    h.Events.SetCapacity(1);
    h.Fx.Clip("asset://anim/walk.sanim", 1.0f, { Event(1, 0.25f, "anim.footstep", GrassInputs()) });
    const DataAssetHandle rig = LoadRig(h.Fx);
    const EntityId first = h.Prop(rig);
    const EntityId second = h.Prop(rig);
    h.Step(30);

    EXPECT_EQ(h.Footsteps.Calls.size(), 1u);
    const std::vector<const AnimDecisionRecord*> a = h.EventRecords(first);
    const std::vector<const AnimDecisionRecord*> b = h.EventRecords(second);
    ASSERT_EQ(a.size() + b.size(), 2u);
    const VerbAdmission firstAdmission = a.empty() ? VerbAdmission::Accepted : a[0]->Admission;
    const VerbAdmission secondAdmission = b.empty() ? VerbAdmission::Accepted : b[0]->Admission;
    EXPECT_TRUE((firstAdmission == VerbAdmission::QueueFull) != (secondAdmission == VerbAdmission::QueueFull));
}

// A record read from a rig that has rebound since is not re-resolved against
// the new binding: it is refused as stale.
TEST(AnimEvents, APendingEventFromAnOlderBindingIsStale)
{
    EventHarness h;
    h.BindFootstep();
    h.Fx.Clip("asset://anim/walk.sanim", 1.0f, { Event(1, 0.25f, "anim.footstep", GrassInputs()) });
    const DataAssetHandle rig = LoadRig(h.Fx);
    const EntityId walker = h.Prop(rig);
    h.Step();

    AnimPendingEvent record;
    record.Producer = walker;
    record.Rig = rig;
    record.RigGeneration = h.Fx.Bound(rig).Generation + 1000;
    record.Content = 0;
    record.Event = 0;
    DrainAnimEvents(h.Fx.Entities, { &record, 1 }, &h.Dispatcher);

    EXPECT_TRUE(h.Footsteps.Calls.empty());
    const std::vector<const AnimDecisionRecord*> records = h.EventRecords(walker);
    ASSERT_FALSE(records.empty());
    EXPECT_EQ(records.back()->Admission, VerbAdmission::StaleBinding);
}
