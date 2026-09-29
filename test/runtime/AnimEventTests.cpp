// Clip events: bound against the rig's authored bindings, and produced as
// invocations of the verbs those bindings name.

#include "AnimFlowFixture.h"
#include "AnimRigFixture.h"

#include <anim/AnimEventSystem.h>
#include <authored/VerbDispatcher.h>
#include <authored/VerbRegistry.h>
#include <world/SimulationAuthority.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    DataFieldSchema VerbField(std::string key, DataFieldKind kind)
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
        { "id": "walk", "behavior": "Anim.Walk", "clip": "asset://anim/walk.sanim" } ] })";

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
                            { VerbField("Surface", DataFieldKind::GameplayTag), VerbField("Volume", DataFieldKind::Float),
                              VerbField("Gain", DataFieldKind::Float) }));
        ASSERT_TRUE(Declare(fx.Verbs(), "test.count",
                            { VerbField("Amount", DataFieldKind::Int), VerbField("Scale", DataFieldKind::Float) }));
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
        { "id": "idle", "behavior": "Anim.Idle", "when": [ { "fact": "Variant", "compare": "eq", "value": 1 } ],
          "clip": "asset://anim/a.sanim" },
        { "id": "idle_2", "behavior": "Anim.Idle", "clip": "asset://anim/b.sanim" } ] })");
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
TEST(AnimEvents, MarksInSkippedTicksAreSkippedAtEveryScope)
{
    // Ticks no pass saw -- a dormant zone, a rig bound late -- are not caught up as a
    // burst, on the authority or anywhere else.
    EventHarness h;
    h.BindFootstep();
    h.Fx.Clip("asset://anim/walk.sanim", 1.0f,
              { Event(1, 0.25f, "anim.footstep", GrassInputs(), AnimEventScope::Cosmetic),
                Event(2, 0.30f, "anim.footstep", GrassInputs(), AnimEventScope::Gameplay) });
    const EntityId walker = h.Prop(LoadRig(h.Fx));

    h.Step(5);
    h.Step(30, false);
    h.Step(5);

    EXPECT_TRUE(h.Footsteps.Calls.empty());
    const std::vector<const AnimDecisionRecord*> records = h.EventRecords(walker);
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0]->EventKey, 1u);
    EXPECT_EQ(records[0]->EventOutcome, AnimEventOutcome::Skipped);
    EXPECT_EQ(records[1]->EventKey, 2u);
    EXPECT_EQ(records[1]->EventOutcome, AnimEventOutcome::Skipped);
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
                    R"({ "rows": [ { "id": "open", "behavior": "Anim.Door.Open", "clip": "asset://anim/door.sanim" } ] })");
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
    h.Events.SetCapacity(AnimEventScope::Cosmetic, 1);
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
    AnimPendingEvents pending;
    pending.Cosmetic.push_back(record);
    DrainAnimEvents(h.Fx.Entities, pending, &h.Dispatcher);

    EXPECT_TRUE(h.Footsteps.Calls.empty());
    const std::vector<const AnimDecisionRecord*> records = h.EventRecords(walker);
    ASSERT_FALSE(records.empty());
    EXPECT_EQ(records.back()->Admission, VerbAdmission::StaleBinding);
}

// -- What a pass crosses ---------------------------------------------------------------

namespace
{
    // The keys of the marks a pass fired, in the order the log has them.
    std::vector<std::uint32_t> FiredKeys(const std::vector<const AnimDecisionRecord*>& records)
    {
        std::vector<std::uint32_t> keys;
        for (const AnimDecisionRecord* record : records)
            if (record->EventOutcome == AnimEventOutcome::Fired && record->Admission == VerbAdmission::Accepted)
                keys.push_back(record->EventKey);
        return keys;
    }
}

// A flow section plays its clip to the end before the next begins: the marks in the
// tick it ends on are crossed, gameplay ones on the authority included, once per loop.
TEST(AnimEvents, AFlowSectionPlaysItsLastMarksBeforeTheNext)
{
    EventHarness h;
    h.BindFootstep();
    // Open is 15 ticks, so its last tick covers (14/60, 15/60] s: both marks.
    h.Fx.Clip("asset://anim/open.sanim", 0.25f,
              { Event(1, 0.96f, "anim.footstep", GrassInputs(), AnimEventScope::Gameplay),
                Event(2, 1.0f, "anim.footstep", GrassInputs(), AnimEventScope::Gameplay) });
    h.Fx.Clip("asset://anim/insert.sanim", 0.5f,
              { Event(3, 1.0f, "anim.footstep", GrassInputs(), AnimEventScope::Gameplay) });
    (void)h.Fx.Load("asset://anim/ev.bindings.sdata", kVerbBindingsTypeName, kBindings);
    const DataAssetHandle rig =
        LoadReloadRig(h.Fx, CountedLoopFlow(), "cancel_section", {}, {}, "asset://anim/ev.bindings.sdata");
    const EntityId prop = h.Prop(rig);
    h.Step();

    AnimRequestDesc desc;
    desc.Source = prop;
    desc.Intent = h.Fx.Tag("anim.intent.reload");
    desc.Params[0] = AnimFactFromInt(2);
    ASSERT_TRUE(IssueAnimRequest(h.Fx.Entities, prop, desc, h.Fx.Now).Accepted());
    h.Step(15 + 2 * 30 + 15 + 5);

    EXPECT_EQ(FiredKeys(h.EventRecords(prop)), (std::vector<std::uint32_t>{ 1, 2, 3, 3 }));
}

// A client's estimate of the authority's tick can repeat or step back. A pass at a
// tick already covered crosses nothing a second time.
TEST(AnimEvents, ARepeatedOrEarlierTickFiresNothingAgain)
{
    EventHarness h;
    h.BindFootstep();
    h.Fx.Clip("asset://anim/walk.sanim", 1.0f, { Event(1, 0.1f, "anim.footstep", GrassInputs()) });
    (void)h.Prop(LoadRig(h.Fx));
    h.Step(10);
    ASSERT_EQ(h.Footsteps.Calls.size(), 1u);

    h.Fx.Now -= 1;
    h.Step();
    h.Fx.Now -= 2;
    h.Step(3);

    EXPECT_EQ(h.Footsteps.Calls.size(), 1u);
}

// Content no pass saw begin -- a late joiner's, a corrected or reconstructed request's
// -- starts crossing where the pass meets it; what went by is recorded as skipped.
TEST(AnimEvents, ContentFirstSeenLongAfterItBeganSkipsWhatWentBy)
{
    EventHarness h;
    h.BindFootstep();
    h.Fx.Clip("asset://anim/door.sanim", 1.0f,
              { Event(1, 0.1f, "anim.footstep", GrassInputs()), Event(2, 0.5f, "anim.footstep", GrassInputs()) });
    (void)h.Fx.Load("asset://anim/ev.bindings.sdata", kVerbBindingsTypeName, kBindings);
    (void)h.Fx.Load("asset://anim/door.requests.sdata", kAnimRequestSchemaType,
                    R"({ "intents": [ { "intent": "Anim.Door.Open", "params": [] } ] })");
    (void)h.Fx.Load("asset://anim/door.behaviors.sdata", kAnimBehaviorSetType,
                    R"({ "behaviors": [ { "tag": "Anim.Door.Open", "kind": "one_shot" } ] })");
    (void)h.Fx.Load("asset://anim/door.slots.sdata", kAnimSlotMapType,
                    R"({ "rows": [ { "id": "open", "behavior": "Anim.Door.Open", "clip": "asset://anim/door.sanim" } ] })");
    const DataAssetHandle rig = h.Fx.Load("asset://anim/door.rig.sdata", kAnimRigType, R"({
        "requests": "asset://anim/door.requests.sdata", "behaviors": [ "asset://anim/door.behaviors.sdata" ],
        "slot_maps": [ "asset://anim/door.slots.sdata" ], "bindings": [ "asset://anim/ev.bindings.sdata" ],
        "layers": [ { "name": "anim.layer.base" } ] })");
    const EntityId door = h.Prop(rig);
    h.Step(3);

    ASSERT_TRUE(h.Fx.Issue(door, "Anim.Door.Open").Accepted());
    h.Step(20, false);
    h.Step(20);

    const std::vector<const AnimDecisionRecord*> records = h.EventRecords(door);
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0]->EventKey, 1u);
    EXPECT_EQ(records[0]->EventOutcome, AnimEventOutcome::Skipped);
    EXPECT_EQ(records[1]->EventKey, 2u);
    EXPECT_EQ(records[1]->EventOutcome, AnimEventOutcome::Fired);
    EXPECT_EQ(h.Footsteps.Calls.size(), 1u);
}

// A row that takes over carrying the phase plays the tick it took over on: a footstep
// in that tick is the new row's, not lost between the two.
TEST(AnimEvents, ACarriedRowPlaysTheTickItTookOver)
{
    EventHarness h;
    h.BindFootstep();
    h.Fx.Clip("asset://anim/walk.sanim", 1.0f);
    h.Fx.Clip("asset://anim/crouch.sanim", 1.0f, { Event(1, 0.5f, "anim.footstep", GrassInputs()) });
    (void)h.Fx.Load("asset://anim/ev.bindings.sdata", kVerbBindingsTypeName, kBindings);
    (void)h.Fx.Load("asset://anim/c.facts.sdata", kAnimFactSchemaType,
                    R"({ "slots": [ { "name": "Crouched", "kind": "bool" } ] })");
    (void)h.Fx.Load("asset://anim/ev.behaviors.sdata", kAnimBehaviorSetType, kBehaviors);
    (void)h.Fx.Load("asset://anim/c.slots.sdata", kAnimSlotMapType, R"({ "rows": [
        { "id": "walk", "behavior": "Anim.Walk", "when": [ { "fact": "Crouched" } ],
          "clip": "asset://anim/crouch.sanim" },
        { "id": "walk_2", "behavior": "Anim.Walk", "clip": "asset://anim/walk.sanim" } ] })");
    const DataAssetHandle rig = h.Fx.Load("asset://anim/c.rig.sdata", kAnimRigType, R"({
        "facts": "asset://anim/c.facts.sdata", "behaviors": [ "asset://anim/ev.behaviors.sdata" ],
        "slot_maps": [ "asset://anim/c.slots.sdata" ], "bindings": [ "asset://anim/ev.bindings.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Walk" } ] })");
    ASSERT_TRUE(h.Fx.Bound(rig).Valid) << AnimRigFixture::Describe(h.Fx.Bound(rig));
    const EntityId walker = h.Fx.Character(rig);
    h.Fx.Entities.RemoveComponent<AnimDecisionLog>(walker);
    h.Fx.Entities.AddComponent(walker, AnimDecisionLog{});

    // Walking from tick 0, the clip reaches 0.5 s on tick 30: crouch then.
    h.Step(30);
    h.Fx.Motion(walker).Crouched = true;
    h.Step();

    ASSERT_EQ(h.Fx.ClipName(walker, h.Fx.Bound(rig)), "asset://anim/crouch.sanim");
    EXPECT_EQ(h.Footsteps.Calls.size(), 1u);
}

// Gameplay and cosmetic crossings queue apart: however much cosmetic traffic a tick
// carries, it cannot take a gameplay event's place.
TEST(AnimEvents, CosmeticTrafficCannotStarveAGameplayEvent)
{
    EventHarness h;
    h.BindFootstep();
    h.Events.SetCapacity(AnimEventScope::Cosmetic, 1);
    h.Fx.Clip("asset://anim/walk.sanim", 1.0f,
              { Event(1, 0.25f, "anim.footstep", GrassInputs(), AnimEventScope::Cosmetic),
                Event(2, 0.25f, "anim.footstep", GrassInputs(), AnimEventScope::Gameplay) });
    const DataAssetHandle rig = LoadRig(h.Fx);
    const EntityId first = h.Prop(rig);
    const EntityId second = h.Prop(rig);
    h.Step(20);

    std::vector<std::uint32_t> fired = FiredKeys(h.EventRecords(first));
    const std::vector<std::uint32_t> secondFired = FiredKeys(h.EventRecords(second));
    fired.insert(fired.end(), secondFired.begin(), secondFired.end());
    EXPECT_EQ(std::ranges::count(fired, 2u), 2) << "both gameplay events fire";
    EXPECT_EQ(std::ranges::count(fired, 1u), 1) << "the cosmetic queue held one";
}

// A gameplay crossing refused for room is a lost effect: counted by the system and
// recorded against the entity, never dropped unseen.
TEST(AnimEvents, AGameplayOverflowIsCountedAndRecorded)
{
    EventHarness h;
    h.BindFootstep();
    h.Events.SetCapacity(AnimEventScope::Gameplay, 1);
    h.Fx.Clip("asset://anim/walk.sanim", 1.0f,
              { Event(2, 0.25f, "anim.footstep", GrassInputs(), AnimEventScope::Gameplay) });
    const DataAssetHandle rig = LoadRig(h.Fx);
    const EntityId first = h.Prop(rig);
    const EntityId second = h.Prop(rig);
    h.Step(20);

    EXPECT_EQ(h.Events.RefusedGameplay(), 1u);
    EXPECT_EQ(h.Events.RefusedCosmetic(), 0u);
    std::size_t queueFull = 0;
    for (const EntityId entity : { first, second })
        for (const AnimDecisionRecord* record : h.EventRecords(entity))
            queueFull += record->Admission == VerbAdmission::QueueFull ? 1u : 0u;
    EXPECT_EQ(queueFull, 1u);
}

// -- Behavior lifecycle -------------------------------------------------------------

namespace
{
    // Records which binding was invoked and the behavior tag it was handed.
    struct LifecycleRecorder
    {
        std::vector<std::pair<VerbBindingKey, GameplayTagId>> Calls;

        VerbAdmission Invoke(const VerbInvocation& invocation)
        {
            GameplayTagId behavior;
            (void)invocation.Arguments->TryGetTag(0, behavior);
            Calls.emplace_back(invocation.Binding, behavior);
            return VerbAdmission::Accepted;
        }
    };

    constexpr std::string_view kLifecycleBindings = R"({ "bindings": [
        { "key": "anim.entered", "verb": "test.lifecycle", "inputs": [ "behavior" ],
          "arguments": { "Behavior": { "input": "behavior" } } },
        { "key": "anim.exited", "verb": "test.lifecycle", "inputs": [ "behavior" ],
          "arguments": { "Behavior": { "input": "behavior" } } },
        { "key": "anim.wrong_input", "verb": "test.lifecycle", "inputs": [ "which" ],
          "arguments": { "Behavior": { "input": "which" } } } ] })";

    // A request-keyed layer idling in Anim.Walk, which announces leaving, and
    // opening a door on request, which announces entering.
    DataAssetHandle LoadLifecycleRig(AnimRigFixture& fx, std::string_view enteredBinding = "anim.entered")
    {
        fx.Clip("asset://anim/walk.sanim", 1.0f);
        fx.Clip("asset://anim/door.sanim", 1.0f);
        (void)fx.Load("asset://anim/lc.bindings.sdata", kVerbBindingsTypeName, kLifecycleBindings);
        (void)fx.Load("asset://anim/lc.requests.sdata", kAnimRequestSchemaType,
                      R"({ "intents": [ { "intent": "Anim.Door.Open", "params": [] } ] })");
        (void)fx.Load("asset://anim/lc.behaviors.sdata", kAnimBehaviorSetType,
                      std::string(R"({ "behaviors": [
                          { "tag": "Anim.Walk", "kind": "cyclic", "on_exited": { "binding": "anim.exited" } },
                          { "tag": "Anim.Door.Open", "kind": "one_shot",
                            "on_entered": { "binding": ")") + std::string(enteredBinding) + R"(" } } ] })");
        (void)fx.Load("asset://anim/lc.slots.sdata", kAnimSlotMapType, R"({ "rows": [
            { "id": "walk", "behavior": "Anim.Walk", "clip": "asset://anim/walk.sanim" },
            { "id": "open", "behavior": "Anim.Door.Open", "clip": "asset://anim/door.sanim" } ] })");
        return fx.Load("asset://anim/lc.rig.sdata", kAnimRigType, R"({
            "requests": "asset://anim/lc.requests.sdata", "behaviors": [ "asset://anim/lc.behaviors.sdata" ],
            "slot_maps": [ "asset://anim/lc.slots.sdata" ], "bindings": [ "asset://anim/lc.bindings.sdata" ],
            "layers": [ { "name": "anim.layer.base", "idle": "Anim.Walk" } ] })");
    }
}

// Leaving a behavior and entering the next are two invocations, exit first,
// each handed its own behavior's tag.
TEST(AnimEvents, ABehaviorChangeAnnouncesTheExitThenTheEntry)
{
    EventHarness h;
    ASSERT_TRUE(Declare(h.Fx.Verbs(), "test.lifecycle", { VerbField("Behavior", DataFieldKind::GameplayTag) }));
    LifecycleRecorder lifecycle;
    const VerbBindingToken token = h.Dispatcher.Bind(h.Fx.Verbs().Find("test.lifecycle"), lifecycle);
    const EntityId door = h.Prop(LoadLifecycleRig(h.Fx));

    h.Step(5);
    EXPECT_TRUE(lifecycle.Calls.empty()) << "Anim.Walk declares no entry event";

    AnimRequestDesc desc;
    desc.Source = door;
    desc.Intent = h.Fx.Tag("Anim.Door.Open");
    ASSERT_TRUE(IssueAnimRequest(h.Fx.Entities, door, desc, h.Fx.Now).Accepted());
    h.Step();

    ASSERT_EQ(lifecycle.Calls.size(), 2u);
    EXPECT_EQ(lifecycle.Calls[0].first, MakeVerbBindingKey("anim.exited"));
    EXPECT_EQ(lifecycle.Calls[0].second, h.Fx.Tag("Anim.Walk"));
    EXPECT_EQ(lifecycle.Calls[1].first, MakeVerbBindingKey("anim.entered"));
    EXPECT_EQ(lifecycle.Calls[1].second, h.Fx.Tag("Anim.Door.Open"));

    const AnimDecisionRecord* exited = h.Fx.LastRecord(door, AnimDecisionCause::BehaviorExited);
    const AnimDecisionRecord* entered = h.Fx.LastRecord(door, AnimDecisionCause::BehaviorEntered);
    ASSERT_NE(exited, nullptr);
    ASSERT_NE(entered, nullptr);
    EXPECT_EQ(exited->Behavior, h.Fx.Tag("Anim.Walk"));
    EXPECT_EQ(entered->Behavior, h.Fx.Tag("Anim.Door.Open"));
    EXPECT_EQ(entered->Admission, VerbAdmission::Accepted);
}

// A lifecycle event supplies the behavior's tag and nothing else, so a
// binding that wants another input cannot be satisfied, and says so.
TEST(AnimEventBinding, ALifecycleBindingTakesOnlyTheBehavior)
{
    AnimRigFixture fx({ "Anim.Walk", "Anim.Door.Open" });
    ASSERT_TRUE(Declare(fx.Verbs(), "test.lifecycle", { VerbField("Behavior", DataFieldKind::GameplayTag) }));
    const AnimBoundRig& bound = fx.Bound(LoadLifecycleRig(fx, "anim.wrong_input"));
    const AnimDiagnostic* invalid = AnimRigFixture::FindCode(bound, "anim.event.input_invalid");
    ASSERT_NE(invalid, nullptr) << AnimRigFixture::Describe(bound);
    EXPECT_EQ(invalid->AssetPath, "asset://anim/lc.behaviors.sdata");
    EXPECT_EQ(invalid->FieldPath, "on_entered");
    const AnimBoundBehavior* door = bound.FindBehavior(fx.Tag("Anim.Door.Open"));
    ASSERT_NE(door, nullptr);
    ASSERT_TRUE(door->Entered.has_value());
    EXPECT_FALSE(door->Entered->Resolved);
}

TEST(AnimEventBinding, ALifecycleEventNamesItsBinding)
{
    AnimRigFixture fx;
    const std::string error = fx.CompileError(kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Walk", "kind": "cyclic", "on_entered": { "scope": "gameplay" } } ] })");
    EXPECT_NE(error.find("on_entered.binding"), std::string::npos) << error;
}

// A clip replaced where it stands -- an editor's unsaved events, a reimport --
// rebinds every rig that plays it, and its new events are what cross.
TEST(AnimEventBinding, AClipReplacedInPlaceRebindsItsRigs)
{
    AnimRigFixture fx({ "Anim.Walk", "Surface.Grass" });
    DeclareVerbs(fx);
    fx.Clip("asset://anim/walk.sanim", 1.0f);
    const DataAssetHandle rig = LoadRig(fx);
    EXPECT_TRUE(fx.Bound(rig).Contents.at(0).Events.empty());

    const AnimationClipHandle clip = fx.Clips.Find("asset://anim/walk.sanim");
    AnimationClipData edited = *fx.Clips.Get(clip);
    edited.Events = { Event(3, 0.5f, "anim.footstep", { Tag("surface", "Surface.Grass"), Const("volume", JsonValue(1.0)) }) };
    ASSERT_TRUE(fx.Clips.ReloadInPlace(clip, std::move(edited)));

    const AnimBoundRig& rebound = fx.Bound(rig);
    ASSERT_EQ(rebound.Contents.at(0).Events.size(), 1u);
    EXPECT_EQ(rebound.Contents[0].Events[0].Key, 3u);
    EXPECT_TRUE(rebound.Contents[0].Events[0].Resolved) << AnimRigFixture::Describe(rebound);

    AnimationClipData otherSkeleton = *fx.Clips.Get(clip);
    otherSkeleton.SkeletonPath = "asset://anim/other.sskel";
    EXPECT_FALSE(fx.Clips.ReloadInPlace(clip, std::move(otherSkeleton)));
}

// A blendspace plays its heaviest sample's marks, crossed in the phase every
// sample shares: once per loop of the mix, however long the mix now is.
TEST(AnimEvents, ABlendspaceCrossesItsHeaviestSamplesMarksOncePerLoop)
{
    EventHarness h;
    h.BindFootstep();
    for (const char* tag : { "Anim.Move" })
        (void)h.Fx.Tags().RegisterTag(tag);
    h.Fx.Clip("asset://anim/walk.sanim", 1.0f, { Event(1, 0.5f, "anim.footstep", GrassInputs()) });
    h.Fx.Clip("asset://anim/run.sanim", 0.5f, { Event(1, 0.5f, "anim.footstep", GrassInputs()) });
    (void)h.Fx.Load("asset://anim/ev.bindings.sdata", kVerbBindingsTypeName, kBindings);
    (void)h.Fx.Load("asset://anim/bs.facts.sdata", kAnimFactSchemaType,
                    R"({ "slots": [ { "name": "Speed", "kind": "float" } ] })");
    (void)h.Fx.Load("asset://anim/bs.space.sdata", kAnimBlendspaceType, R"({
        "axes": [ { "fact": "Speed", "min": 0, "max": 4 } ],
        "samples": [ { "clip": "asset://anim/walk.sanim", "at": [ 1 ] },
                     { "clip": "asset://anim/run.sanim", "at": [ 3 ] } ] })");
    (void)h.Fx.Load("asset://anim/bs.behaviors.sdata", kAnimBehaviorSetType,
                    R"({ "behaviors": [ { "tag": "Anim.Move", "kind": "cyclic" } ] })");
    (void)h.Fx.Load("asset://anim/bs.slots.sdata", kAnimSlotMapType,
                    R"({ "rows": [ { "id": "move", "behavior": "Anim.Move", "blendspace": "asset://anim/bs.space.sdata" } ] })");
    const DataAssetHandle rig = h.Fx.Load("asset://anim/bs.rig.sdata", kAnimRigType, R"({
        "facts": "asset://anim/bs.facts.sdata", "behaviors": [ "asset://anim/bs.behaviors.sdata" ],
        "slot_maps": [ "asset://anim/bs.slots.sdata" ], "bindings": [ "asset://anim/ev.bindings.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Move" } ] })");
    ASSERT_TRUE(h.Fx.Bound(rig).Valid) << AnimRigFixture::Describe(h.Fx.Bound(rig));
    const EntityId mover = h.Fx.Character(rig, AnimTestMotion{ .Speed = 1.0f });

    // At walk alone the mix is 1 s: a footstep half way, once a loop.
    h.Step(120);
    ASSERT_EQ(h.Footsteps.Calls.size(), 2u);
    EXPECT_EQ(h.Footsteps.Calls[0].Tick, 30u);
    EXPECT_EQ(h.Footsteps.Calls[1].Tick, 90u);

    // At run alone it is 0.5 s: twice as often, from where the phase was.
    h.Footsteps.Calls.clear();
    h.Fx.Motion(mover).Speed = 3.0f;
    h.Step(60);
    ASSERT_EQ(h.Footsteps.Calls.size(), 2u);
    EXPECT_EQ(h.Footsteps.Calls[1].Tick - h.Footsteps.Calls[0].Tick, 30u);
}
