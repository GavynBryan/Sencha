// Presentation layers: a rig layer declared `presentation` plays what this machine
// presents through anim.present and nothing the simulation asks for, its set
// exists only where something presents, and the binder keeps root motion and
// gameplay events off whatever such a request can make it play.

#include "AnimRigFixture.h"

#include <anim/AnimPresentationRequests.h>
#include <anim/AnimRequestVerbs.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbBindingData.h>
#include <authored/VerbRegistry.h>
#include <time/SimClock.h>

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <vector>

namespace
{
    constexpr const char* kTags[] = {
        "anim.layer.reaction", "Anim.Idle", "Anim.Reaction.None", "anim.intent.flinch", "anim.intent.reload",
    };

    constexpr std::string_view kPlainFlinch = R"({ "tag": "anim.intent.flinch", "kind": "one_shot" })";
    constexpr std::string_view kPresentationLayer = R"("presentation": true, "idle": "Anim.Reaction.None")";

    struct PresentationFixture : AnimRigFixture
    {
        DataAssetHandle Rig;
        EntityId Entity;

        // `flinch` declares the flinch behavior, `reaction` the reaction layer's fields
        // after its name, and `flinchEvents` the flinch clip's events.
        explicit PresentationFixture(std::string_view flinch = kPlainFlinch,
                                     std::string_view reaction = kPresentationLayer,
                                     std::vector<AnimationClipEvent> flinchEvents = {})
        {
            for (const char* tag : kTags)
                (void)Tags().RegisterTag(tag);
            VerbDefinition mark;
            mark.Name = "test.mark";
            mark.Scope = AuthoredScope::Cosmetic;
            VerbRegistrationScope scope(Verbs(), "test");
            (void)scope.Declare(std::move(mark));
            EXPECT_TRUE(scope.Commit());

            Clip("asset://anim/idle.sanim", 1.0f);
            Clip("asset://anim/flinch.sanim", 0.5f, std::move(flinchEvents));
            Clip("asset://anim/reload.sanim", 1.0f);
            (void)Load("asset://anim/p.bindings.sdata", kVerbBindingsTypeName,
                       R"({ "bindings": [ { "key": "anim.mark", "verb": "test.mark" } ] })");
            (void)Load("asset://anim/p.requests.sdata", kAnimRequestSchemaType, R"({ "intents": [
                { "intent": "anim.intent.flinch", "params": [] }, { "intent": "anim.intent.reload", "params": [] } ] })");
            (void)Load("asset://anim/p.behaviors.sdata", kAnimBehaviorSetType,
                       std::format(R"({{ "behaviors": [ {{ "tag": "Anim.Idle", "kind": "cyclic" }},
                {{ "tag": "Anim.Reaction.None", "kind": "cyclic" }}, {},
                {{ "tag": "anim.intent.reload", "kind": "one_shot" }} ] }})",
                                   flinch));
            (void)Load("asset://anim/p.slots.sdata", kAnimSlotMapType, R"({ "rows": [
                { "id": "idle", "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
                { "id": "none", "behavior": "Anim.Reaction.None", "clip": "asset://anim/idle.sanim" },
                { "id": "flinch", "behavior": "anim.intent.flinch", "clip": "asset://anim/flinch.sanim" },
                { "id": "reload", "behavior": "anim.intent.reload", "clip": "asset://anim/reload.sanim" } ] })");
            Rig = Load("asset://anim/p.rig.sdata", kAnimRigType, std::format(R"({{
                "requests": "asset://anim/p.requests.sdata", "behaviors": [ "asset://anim/p.behaviors.sdata" ],
                "slot_maps": [ "asset://anim/p.slots.sdata" ], "bindings": [ "asset://anim/p.bindings.sdata" ],
                "layers": [ {{ "name": "anim.layer.base", "idle": "Anim.Idle" }},
                            {{ "name": "anim.layer.reaction", {} }} ] }})",
                                                                             reaction));
            Entity = Character(Rig);
        }

        AnimPresentationRequests* Presented()
        {
            return Entities.IsRegistered<AnimPresentationRequests>() ? Entities.TryGet<AnimPresentationRequests>(Entity)
                                                                      : nullptr;
        }

        AnimRequestResult Present(std::string_view intent, std::uint32_t ticks)
        {
            AnimRequestDesc desc;
            desc.Source = Entity;
            desc.Intent = Tag(intent);
            desc.Lifetime = AnimRequestLifetime::Fixed;
            desc.FixedTicks = ticks;
            return IssueAnimRequest(Presented()->Set, desc, Now, nullptr);
        }

        VerbAdmission PresentByVerb(EntityId target, std::string_view intent)
        {
            FixedSimulationLoop clock(kTick);
            AnimRequestOperations operations(Entities, clock);
            AuthoredArguments arguments(4);
            arguments.Set(0, AuthoredValue::Entity(target));
            arguments.Set(1, AuthoredValue::Tag(Tag(intent)));
            arguments.Set(2, AuthoredValue::Enum("fixed"));
            arguments.Set(3, AuthoredValue::Int(10));
            VerbInvocation invocation;
            invocation.Arguments = &arguments;
            invocation.Tick = Now;
            return operations.Present(invocation);
        }

        [[nodiscard]] bool Refuses(std::string_view needle) const
        {
            const AnimBoundRig* bound = const_cast<PresentationFixture*>(this)->Bindings().Resolve(Rig, Entities);
            for (const AnimDiagnostic& diagnostic : bound->Diagnostics)
                if (diagnostic.Code == "anim.rig.presentation_layer" && diagnostic.Message.find(needle) != std::string::npos)
                    return !bound->Valid;
            return false;
        }
    };
}

TEST(AnimPresentationLayer, APresentedIntentPlaysOnThePresentationLayerAlone)
{
    PresentationFixture fx;
    ASSERT_TRUE(fx.Bound(fx.Rig).Valid) << AnimRigFixture::Describe(fx.Bound(fx.Rig));
    fx.Tick();
    ASSERT_NE(fx.Presented(), nullptr) << "a rig with a presentation layer carries the set where poses are presented";
    ASSERT_TRUE(fx.Present("anim.intent.flinch", 10).Accepted());
    fx.Tick();
    EXPECT_EQ(fx.BehaviorName(fx.Entity, 1), "anim.intent.flinch");
    EXPECT_EQ(fx.BehaviorName(fx.Entity, 0), "Anim.Idle") << "an ordinary layer never reads what is presented";
    fx.Tick(12);
    EXPECT_EQ(fx.BehaviorName(fx.Entity, 1), "Anim.Reaction.None") << "and it ends with its lifetime";
}

TEST(AnimPresentationLayer, TheSimulationsRequestsNeverReachAPresentationLayer)
{
    PresentationFixture fx;
    fx.Tick();
    ASSERT_TRUE(fx.Issue(fx.Entity, "anim.intent.flinch").Accepted());
    fx.Tick();
    EXPECT_EQ(fx.BehaviorName(fx.Entity, 0), "anim.intent.flinch");
    EXPECT_EQ(fx.BehaviorName(fx.Entity, 1), "Anim.Reaction.None");
    const AnimRequestSet& simulation = *fx.Entities.TryGet<AnimRequestSet>(fx.Entity);
    ASSERT_TRUE(fx.Present("anim.intent.reload", 10).Accepted());
    EXPECT_EQ(std::ranges::count_if(simulation.Records, [](const AnimRequest& request) { return request.Occupied; }), 1)
        << "presenting writes nothing the simulation replicates";
}

TEST(AnimPresentationLayer, OnlyAMachineThatPresentsCarriesTheSet)
{
    PresentationFixture fx;
    AnimRigCompositionSystem headless{ false };
    headless.Compose(fx.Entities);
    EXPECT_EQ(fx.Presented(), nullptr);
    EXPECT_EQ(fx.PresentByVerb(fx.Entity, "anim.intent.flinch"), VerbAdmission::Refused)
        << "nothing presents here, so there is nothing to present on";
    fx.Tick();
    ASSERT_NE(fx.Presented(), nullptr);
    EXPECT_EQ(fx.PresentByVerb(fx.Entity, "anim.intent.flinch"), VerbAdmission::Accepted);
    const AnimRequest& written = fx.Presented()->Set.Records[0];
    EXPECT_EQ(written.Lifetime, AnimRequestLifetime::Fixed);
    EXPECT_EQ(written.FixedTicks, 10u);
}

TEST(AnimPresentationLayer, APresentationLayerIsRequestKeyed)
{
    PresentationFixture fx(kPlainFlinch, R"("presentation": true, "selector": "asset://anim/p.selector.sdata")");
    (void)fx.Load("asset://anim/p.selector.sdata", kAnimSelectorType,
                  R"({ "rules": [ { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Idle" } ] })");
    EXPECT_TRUE(fx.Refuses("takes no selector"));
}

TEST(AnimPresentationLayer, NothingItCanPlayMovesABody)
{
    PresentationFixture fx(R"({ "tag": "anim.intent.flinch", "kind": "one_shot", "root_motion": true })");
    EXPECT_TRUE(fx.Refuses("'anim.intent.flinch' has root motion"));
}

TEST(AnimPresentationLayer, NothingItCanPlayFiresAGameplayEvent)
{
    AnimationClipEvent mark;
    mark.Key = 1;
    mark.Time = 0.5f;
    mark.Binding = "anim.mark";
    mark.Scope = AuthoredScope::Gameplay;
    PresentationFixture fx(kPlainFlinch, kPresentationLayer, { mark });
    EXPECT_TRUE(fx.Refuses("fires gameplay event 'anim.mark'"));

    mark.Scope = AuthoredScope::Cosmetic;
    PresentationFixture cosmetic(kPlainFlinch, kPresentationLayer, { mark });
    EXPECT_TRUE(cosmetic.Bound(cosmetic.Rig).Valid) << AnimRigFixture::Describe(cosmetic.Bound(cosmetic.Rig));
}
