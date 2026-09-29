// One layered character under one scenario: locomotion, a masked upper-body
// reload, cancellation, a lifecycle binding, clip events, a blend override,
// and in the session lab a late join and a correction.

#include "authoring/AnimationPreviewSession.h"
#include "authoring/AnimationScenario.h"
#include "authoring/AnimationSessionLab.h"

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimBlendOverrides.h>
#include <anim/AnimFactSchema.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimRigData.h>
#include <anim/AnimSelectorData.h>
#include <anim/AnimSlotMapData.h>
#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <authored/VerbBindingData.h>
#include <authored/VerbRegistry.h>
#include <authored/WorldVocabulary.h>
#include <core/json/JsonParser.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace
{
    constexpr std::string_view kRig = "asset://tp/character.rig.sdata";
    constexpr std::string_view kSkeleton = "asset://tp/character.sskel";
    constexpr AnimTick kWalk = 30;
    constexpr AnimTick kReload = 60;
    constexpr AnimTick kSecondReload = 150;
    constexpr AnimTick kCancel = kSecondReload + 20;
    constexpr AnimTick kStop = 220;

    struct Exercise
    {
        DataAssetTypeRegistry Types;
        DataSchemaRegistry Schemas;
        DataAssetCache Data;
        SkeletonCache Skeletons;
        AnimationClipCache Clips;

        Exercise()
        {
            RegisterAnimFactSchema(Types, Schemas);
            RegisterAnimRequestSchema(Types, Schemas);
            RegisterAnimRigData(Types, Schemas);
            RegisterAnimBehaviorSet(Types, Schemas);
            RegisterAnimSelectorData(Types, Schemas);
            RegisterAnimSlotMapData(Types, Schemas);
            RegisterAnimBlendOverrides(Types, Schemas);
            RegisterVerbBindingData(Types, Schemas);

            SkeletonData skeleton;
            for (const auto& [name, parent] : { std::pair{ "root", -1 }, std::pair{ "spine", 0 }, std::pair{ "arm", 1 } })
            {
                SkeletonJoint joint;
                joint.Name = name;
                joint.ParentIndex = parent;
                skeleton.Joints.push_back(joint);
            }
            (void)Skeletons.Register(kSkeleton, std::move(skeleton));
            Clip("asset://tp/idle.sanim", 2.0f);
            Clip("asset://tp/walk.sanim", 1.0f);
            Clip("asset://tp/rest.sanim", 2.0f);
            // The magazine leaves at 0.31 and the reload lands at 0.91: off the tick grid.
            AnimationClipEvent magOut;
            magOut.Key = 1;
            magOut.Time = 0.31f;
            magOut.Binding = "anim.mag_out";
            AnimationClipEvent done;
            done.Key = 2;
            done.Time = 0.91f;
            done.Binding = "weapon.reload_done";
            done.Scope = AnimEventScope::Gameplay;
            Clip("asset://tp/reload.sanim", 1.0f, { magOut, done });

            Load("asset://tp/character.facts.sdata", kAnimFactSchemaType,
                 R"({ "slots": [ { "name": "Speed", "kind": "float" } ] })");
            Load("asset://tp/character.requests.sdata", kAnimRequestSchemaType,
                 R"({ "intents": [ { "intent": "Anim.Weapon.Reload", "params": [] } ] })");
            Load("asset://tp/character.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
                { "tag": "Anim.Locomotion.Idle", "kind": "cyclic" },
                { "tag": "Anim.Locomotion.Walk", "kind": "cyclic" },
                { "tag": "Anim.Upper.Rest", "kind": "cyclic" },
                { "tag": "Anim.Weapon.Reload", "kind": "one_shot",
                  "latch": { "mode": "until_request_ends", "interruptible_by": "never", "on_request_cancel": "abort" },
                  "on_entered": { "binding": "weapon.reload_started" } } ] })");
            Load("asset://tp/character.slots.sdata", kAnimSlotMapType, R"({ "rows": [
                { "id": "idle", "behavior": "Anim.Locomotion.Idle", "clip": "asset://tp/idle.sanim" },
                { "id": "walk", "behavior": "Anim.Locomotion.Walk", "clip": "asset://tp/walk.sanim" },
                { "id": "rest", "behavior": "Anim.Upper.Rest", "clip": "asset://tp/rest.sanim" },
                { "id": "reload", "behavior": "Anim.Weapon.Reload", "clip": "asset://tp/reload.sanim" } ] })");
            Load("asset://tp/locomotion.selector.sdata", kAnimSelectorType, R"({ "rules": [
                { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Locomotion.Idle" },
                { "name": "walk", "priority": 10, "enter": [ { "fact": "Speed", "compare": "gt", "value": 0.1 } ],
                  "behavior": "Anim.Locomotion.Walk" } ] })");
            Load("asset://tp/upper.selector.sdata", kAnimSelectorType, R"({ "rules": [
                { "name": "rest", "priority": 0, "enter": [], "behavior": "Anim.Upper.Rest" },
                { "name": "reload", "priority": 50, "enter": [ { "request": "Anim.Weapon.Reload" } ],
                  "behavior": "Anim.Weapon.Reload" },
                { "name": "shown", "priority": 10, "weight": 1, "enter": [ { "request": "Anim.Weapon.Reload" } ] },
                { "name": "hidden", "priority": 0, "weight": 0, "enter": [] } ] })");
            Load("asset://tp/character.overrides.sdata", kAnimBlendOverridesType, R"({ "overrides": [
                { "from": "Anim.Locomotion.Walk", "to": "Anim.Locomotion.Idle",
                  "blend": { "in": "crossfade", "in_ms": 200 } } ] })");
            Load("asset://tp/character.bindings.sdata", kVerbBindingsTypeName, R"({ "bindings": [
                { "key": "anim.mag_out", "verb": "test.mag_out", "arguments": {} },
                { "key": "weapon.reload_done", "verb": "test.reload_done", "arguments": {} },
                { "key": "weapon.reload_started", "verb": "test.reload_started", "arguments": {} } ] })");
            Load(kRig, kAnimRigType, R"({
                "skeleton": "asset://tp/character.sskel",
                "facts": "asset://tp/character.facts.sdata",
                "requests": "asset://tp/character.requests.sdata",
                "behaviors": [ "asset://tp/character.behaviors.sdata" ],
                "slot_maps": [ "asset://tp/character.slots.sdata" ],
                "blend_overrides": [ "asset://tp/character.overrides.sdata" ],
                "bindings": [ "asset://tp/character.bindings.sdata" ],
                "layers": [
                    { "name": "anim.layer.base", "selector": "asset://tp/locomotion.selector.sdata",
                      "idle": "Anim.Locomotion.Idle" },
                    { "name": "anim.layer.upper", "selector": "asset://tp/upper.selector.sdata",
                      "idle": "Anim.Upper.Rest", "mask": [ { "joint": "spine" } ] } ] })");
        }

        void Clip(std::string_view path, float seconds, std::vector<AnimationClipEvent> events = {})
        {
            AnimationClipData clip;
            clip.SkeletonPath = std::string(kSkeleton);
            clip.DurationSeconds = seconds;
            clip.Events = std::move(events);
            (void)Clips.Register(path, std::move(clip), Skeletons.AcquireOwned(kSkeleton));
        }

        void Load(std::string_view path, std::string_view type, std::string_view json)
        {
            const DataAssetCompileResult compiled = Types.Find(type)->Compile(*JsonParse(json));
            ASSERT_TRUE(compiled.IsValid()) << path << ": " << compiled.Error;
            (void)Data.Register(path, std::string(type), compiled.Value);
        }

        // What the game module would declare.
        static void Vocabulary(World& world)
        {
            GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
            for (const char* name : { "Anim.Locomotion.Idle", "Anim.Locomotion.Walk", "Anim.Upper.Rest",
                                      "Anim.Weapon.Reload" })
                (void)tags.RegisterTag(name);
            VerbRegistry& verbs = InstallVerbRegistry(world);
            VerbRegistrationScope scope(verbs, "test");
            for (const char* name : { "test.mag_out", "test.reload_done", "test.reload_started" })
            {
                VerbDefinition verb;
                verb.Name = name;
                (void)scope.Declare(std::move(verb));
            }
            (void)scope.Commit();
        }

        // Walk, reload through, reload again and let go partway, stop.
        static AnimationScenario Scenario()
        {
            AnimationScenario scenario;
            scenario.Name = "third_person";
            scenario.RigPath = std::string(kRig);
            scenario.Participants = { "player" };
            scenario.Recorders = { "test.mag_out", "test.reload_done", "test.reload_started" };
            scenario.Inputs = { { "Speed", AnimationScenarioValue::FromNumber(0.0) } };
            const auto speed = [&](AnimTick tick, double value) {
                AnimationScenarioAction action;
                action.Tick = tick;
                action.Kind = AnimationScenarioActionKind::SetFact;
                action.Fact = "Speed";
                action.Value = AnimationScenarioValue::FromNumber(value);
                scenario.Append(action);
            };
            const auto reload = [&](AnimTick tick, AnimRequestLifetime lifetime, std::uint32_t ticks) {
                AnimationScenarioAction action;
                action.Tick = tick;
                action.Kind = AnimationScenarioActionKind::IssueRequest;
                action.Participant = "player";
                action.Intent = "Anim.Weapon.Reload";
                action.Lifetime = lifetime;
                action.FixedTicks = ticks;
                scenario.Append(action);
            };
            speed(kWalk, 1.5);
            reload(kReload, AnimRequestLifetime::Fixed, 60);
            reload(kSecondReload, AnimRequestLifetime::Held, 0);
            AnimationScenarioAction cancel;
            cancel.Tick = kCancel;
            cancel.Kind = AnimationScenarioActionKind::CancelRequest;
            cancel.Participant = "player";
            cancel.Intent = "Anim.Weapon.Reload";
            cancel.Reason = AnimCancelReason::Released;
            scenario.Append(cancel);
            speed(kStop, 0.0);
            return scenario;
        }
    };

    const AnimationPreviewTickRecord& At(const AnimationPreviewSession& session, AnimTick tick)
    {
        const auto found = std::ranges::find(session.History(), tick, &AnimationPreviewTickRecord::Tick);
        EXPECT_NE(found, session.History().end()) << tick;
        return *found;
    }

    std::string Behavior(const AnimationPreviewSession& session, AnimTick tick, std::size_t layer)
    {
        return std::string(session.Tags()->GetName(At(session, tick).Layers[layer].Behavior));
    }

    std::vector<std::string> Invoked(const AnimationPreviewSession& session)
    {
        std::vector<std::string> invoked;
        for (const AnimationPreviewTickRecord& tick : session.History())
            for (const AnimationPreviewInvocation& invocation : tick.Invocations)
                invoked.push_back(std::format("{} {}", tick.Tick, invocation.Verb));
        return invoked;
    }
}

TEST(AnimationThirdPersonExercise, TheCharacterPlaysEveryMechanismOnItsTick)
{
    Exercise exercise;
    AnimationPreviewSession session(exercise.Data, &exercise.Clips, Exercise::Vocabulary, &exercise.Skeletons);
    ASSERT_TRUE(session.Open(Exercise::Scenario()));
    const AnimBoundRig* rig = session.Rig();
    ASSERT_NE(rig, nullptr);
    ASSERT_TRUE(rig->Valid) << FormatAnimDiagnostic(rig->Diagnostics.front());
    session.RunTo(kStop + 30);
    EXPECT_TRUE(session.ScenarioProblems().empty()) << FormatAnimDiagnostic(session.ScenarioProblems().front());

    // The upper body is masked from the spine down: the root stays the base's.
    ASSERT_EQ(rig->Layers.size(), 2u);
    EXPECT_FALSE(rig->Layers[1].Covers(0));
    EXPECT_TRUE(rig->Layers[1].Covers(1));
    EXPECT_TRUE(rig->Layers[1].Covers(2));

    // Locomotion follows speed, underneath whatever the upper body does.
    EXPECT_EQ(Behavior(session, kWalk - 1, 0), "Anim.Locomotion.Idle");
    EXPECT_EQ(Behavior(session, kWalk, 0), "Anim.Locomotion.Walk");
    EXPECT_EQ(Behavior(session, kReload + 10, 0), "Anim.Locomotion.Walk");

    // The upper body is hidden at rest and shown while it reloads.
    EXPECT_EQ(Behavior(session, kReload - 1, 1), "Anim.Upper.Rest");
    EXPECT_FLOAT_EQ(At(session, kReload - 1).Layers[1].Weight, 0.0f);
    EXPECT_EQ(Behavior(session, kReload, 1), "Anim.Weapon.Reload");
    EXPECT_FLOAT_EQ(At(session, kReload).Layers[1].Weight, 1.0f);
    EXPECT_EQ(Behavior(session, kReload + 70, 1), "Anim.Upper.Rest");

    // Let go partway, the second reload aborts at once.
    EXPECT_EQ(Behavior(session, kCancel - 1, 1), "Anim.Weapon.Reload");
    EXPECT_EQ(Behavior(session, kCancel, 1), "Anim.Upper.Rest");

    // Entering a reload is announced each time; the first plays both clip
    // events, the aborted one only the magazine it got to.
    EXPECT_EQ(Invoked(session), (std::vector<std::string>{
                                    std::format("{} test.reload_started", kReload),
                                    std::format("{} test.mag_out", kReload + 19),
                                    std::format("{} test.reload_done", kReload + 55),
                                    std::format("{} test.reload_started", kSecondReload),
                                    std::format("{} test.mag_out", kSecondReload + 19) }));

    // Stopping walks back to idle by the override's crossfade.
    const AnimationPreviewTickRecord& stopped = At(session, kStop);
    EXPECT_EQ(Behavior(session, kStop, 0), "Anim.Locomotion.Idle");
    const auto blend = std::ranges::find_if(stopped.Decisions, [](const AnimDecisionRecord& record) {
        return record.Cause == AnimDecisionCause::BlendApplied && record.Layer == 0;
    });
    ASSERT_NE(blend, stopped.Decisions.end());
    EXPECT_TRUE(blend->BlendOverridden);
    EXPECT_EQ(blend->Blend, AnimBlendMode::Crossfade);
    EXPECT_FLOAT_EQ(blend->BlendSeconds, 0.2f);
}

// A joiner stays in step through a late-arriving cancel; a refused guess is
// taken back when the refusal arrives. The rest loop's phase is then its own,
// as docs/gameplay/animation.md explains.
TEST(AnimationThirdPersonExercise, ALateJoinerAndACorrectionEndWhereTheAuthorityIs)
{
    Exercise exercise;
    AnimationSessionLab lab(exercise.Data, &exercise.Clips, Exercise::Vocabulary, &exercise.Skeletons);
    ASSERT_TRUE(lab.Open(Exercise::Scenario(), { .JoinTick = kReload + 25, .LatencyTicks = 2, .LossPercent = 10 },
                         { { .Tick = kSecondReload + 60, .Participant = "player", .Intent = "Anim.Weapon.Reload",
                             .AuthorityTick = kSecondReload + 63, .Confirmed = false } }))
        << (lab.Problems().empty() ? std::string() : FormatAnimDiagnostic(lab.Problems().front()));
    lab.RunTo(kStop + 30);

    const auto at = [&](AnimTick tick) { return lab.Ticks()[static_cast<std::size_t>(tick)]; };
    ASSERT_TRUE(lab.JoinedAt().has_value());
    const AnimTick joined = *lab.JoinedAt();
    EXPECT_GE(joined, kReload + 25);
    EXPECT_LT(joined, kReload + 40) << "the joiner arrives inside the reload";
    for (AnimTick tick = joined; tick < kSecondReload + 60; ++tick)
    {
        const auto inFlight = [&](AnimTick sent) { return tick >= sent && tick < sent + 2; };
        EXPECT_TRUE(inFlight(kSecondReload) || inFlight(kCancel) || at(tick).Agrees)
            << tick << ": only a request or cancel still on its way may put it behind";
    }
    EXPECT_FALSE(at(kSecondReload + 61).Agrees) << "the client plays its guess";

    const auto sameContent = [&](AnimTick tick) {
        const auto find = [&](const AnimationPreviewSession& session) {
            return std::ranges::find(session.History(), tick, &AnimationPreviewTickRecord::Tick);
        };
        const auto here = find(lab.Authority());
        const auto there = find(lab.Client());
        if (here == lab.Authority().History().end() || there == lab.Client().History().end())
            return false;
        for (std::size_t l = 0; l < here->Layers.size(); ++l)
            if (lab.Authority().Tags()->GetName(here->Layers[l].Behavior)
                    != lab.Client().Tags()->GetName(there->Layers[l].Behavior)
                || here->Layers[l].Content != there->Layers[l].Content)
                return false;
        return true;
    };
    const AnimTick refusalArrives = kSecondReload + 63 + 2;
    for (AnimTick tick = refusalArrives + 1; tick <= kStop + 30; ++tick)
        EXPECT_TRUE(sameContent(tick)) << tick;
    EXPECT_EQ(lab.PendingPredictions(), 0u);
}
