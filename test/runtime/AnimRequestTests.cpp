// The request set's rules -- supersession, impulse deduplication, capacity
// without eviction, the primary record, cancel visibility and tail retention --
// decide identically for every caller on every machine. These pin each one.

#include <gtest/gtest.h>

#include <anim/AnimRequestSchema.h>
#include <anim/AnimRequests.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimationRegistration.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/json/JsonParser.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/ComponentRegistrar.h>

#include <string>
#include <vector>

namespace
{
    struct RequestFixture
    {
        World Entities;
        GameplayTagRegistry* Tags = nullptr;
        std::vector<EntityId> Sources;

        RequestFixture()
        {
            Tags = &Entities.AddResource<GameplayTagRegistry>();
            ComponentRegistrar registrar(Entities);
            RegisterAnimationComponents(registrar);
            InstallAnimationVocabulary(Entities);
            for (int i = 0; i < 10; ++i)
                Sources.push_back(Entities.CreateEntity());
        }

        GameplayTagId Tag(std::string_view name)
        {
            if (const GameplayTagId found = Tags->FindTag(name); found.IsValid())
                return found;
            return Tags->RegisterTag(name).value();
        }

        AnimRequestDesc Desc(std::size_t source, std::string_view intent,
                             AnimRequestLifetime lifetime = AnimRequestLifetime::Held)
        {
            AnimRequestDesc desc;
            desc.Source = Sources[source];
            desc.Intent = Tag(intent);
            desc.Lifetime = lifetime;
            return desc;
        }
    };

    std::size_t Occupied(const AnimRequestSet& set)
    {
        std::size_t count = 0;
        for (const AnimRequest& request : set.Records)
            count += request.Occupied ? 1 : 0;
        return count;
    }
}

TEST(AnimRequests, CapacityRejectsWithoutEvicting)
{
    RequestFixture fx;
    AnimRequestSet set;
    AnimDecisionLog log;
    for (std::size_t i = 0; i < kAnimRequestCapacity; ++i)
    {
        const AnimRequestResult result =
            IssueAnimRequest(set, fx.Desc(i, "anim.intent.a"), 1, &log);
        ASSERT_EQ(result.Status, AnimRequestStatus::Accepted) << i;
        EXPECT_EQ(result.Id.Sequence, i + 1);
    }

    const AnimRequestResult full = IssueAnimRequest(set, fx.Desc(9, "anim.intent.a"), 2, &log);
    EXPECT_EQ(full.Status, AnimRequestStatus::Rejected);
    EXPECT_EQ(full.Reject, AnimRejectReason::Capacity);
    EXPECT_EQ(Occupied(set), kAnimRequestCapacity);
    for (const AnimRequest& request : set.Records)
        EXPECT_EQ(request.StartTick, 1u);

    const AnimDecisionRecord& last = log.At(log.Size() - 1);
    EXPECT_EQ(last.Cause, AnimDecisionCause::RequestRejected);
    EXPECT_EQ(last.RejectReason, AnimRejectReason::Capacity);
}

TEST(AnimRequests, TheSameSourceAndIntentSupersedesInPlace)
{
    RequestFixture fx;
    AnimRequestSet set;
    AnimDecisionLog log;
    const AnimRequestResult first = IssueAnimRequest(set, fx.Desc(0, "anim.intent.attack"), 1, &log);
    (void)IssueAnimRequest(set, fx.Desc(1, "anim.intent.attack"), 1, &log);

    AnimRequestDesc next = fx.Desc(0, "anim.intent.attack");
    next.Params[0] = 2;
    const AnimRequestResult second = IssueAnimRequest(set, next, 5, &log);
    EXPECT_EQ(second.Status, AnimRequestStatus::Superseded);
    EXPECT_NE(second.Id, first.Id);
    EXPECT_EQ(Occupied(set), 2u);

    const AnimRequest* primary = FindPrimaryAnimRequest(set, fx.Tag("anim.intent.attack"), 5);
    ASSERT_NE(primary, nullptr);
    EXPECT_EQ(primary->Id, second.Id);
    EXPECT_EQ(primary->Params[0], 2u);

    EXPECT_EQ(log.At(log.Size() - 2).Cause, AnimDecisionCause::RequestSuperseded);
    EXPECT_EQ(log.At(log.Size() - 2).Request, first.Id);
    EXPECT_EQ(log.At(log.Size() - 1).Cause, AnimDecisionCause::RequestAdded);
}

TEST(AnimRequests, AnImpulseIsDeduplicatedPerTickAndLivesOneTick)
{
    RequestFixture fx;
    AnimRequestSet set;
    const AnimRequestResult first =
        IssueAnimRequest(set, fx.Desc(0, "anim.intent.flinch", AnimRequestLifetime::Impulse), 4, nullptr);
    const AnimRequestResult again =
        IssueAnimRequest(set, fx.Desc(0, "anim.intent.flinch", AnimRequestLifetime::Impulse), 4, nullptr);
    EXPECT_EQ(again.Status, AnimRequestStatus::Deduplicated);
    EXPECT_EQ(again.Id, first.Id);
    EXPECT_EQ(Occupied(set), 1u);

    EXPECT_NE(FindPrimaryAnimRequest(set, fx.Tag("anim.intent.flinch"), 4), nullptr);
    EXPECT_EQ(FindPrimaryAnimRequest(set, fx.Tag("anim.intent.flinch"), 5), nullptr);

    const AnimRequestResult next =
        IssueAnimRequest(set, fx.Desc(0, "anim.intent.flinch", AnimRequestLifetime::Impulse), 5, nullptr);
    EXPECT_EQ(next.Status, AnimRequestStatus::Accepted);
    EXPECT_EQ(Occupied(set), 1u); // the expired impulse was pruned first
}

TEST(AnimRequests, AFixedRequestExpiresOnItsOwn)
{
    RequestFixture fx;
    AnimRequestSet set;
    AnimDecisionLog log;
    AnimRequestDesc desc = fx.Desc(0, "anim.intent.emote", AnimRequestLifetime::Fixed);
    desc.FixedTicks = 3;
    (void)IssueAnimRequest(set, desc, 10, &log);

    const GameplayTagId emote = fx.Tag("anim.intent.emote");
    EXPECT_NE(FindPrimaryAnimRequest(set, emote, 12), nullptr);
    EXPECT_EQ(FindPrimaryAnimRequest(set, emote, 13), nullptr);

    PruneAnimRequests(set, 13, &log);
    EXPECT_EQ(Occupied(set), 0u);
    EXPECT_EQ(log.At(log.Size() - 1).Cause, AnimDecisionCause::RequestExpired);
}

TEST(AnimRequests, ThePrimaryRecordIsNewestThenHighestSequence)
{
    RequestFixture fx;
    AnimRequestSet set;
    const GameplayTagId aim = fx.Tag("anim.intent.aim");
    (void)IssueAnimRequest(set, fx.Desc(0, "anim.intent.aim"), 3, nullptr);
    const AnimRequestResult newest = IssueAnimRequest(set, fx.Desc(1, "anim.intent.aim"), 7, nullptr);
    EXPECT_EQ(FindPrimaryAnimRequest(set, aim, 7)->Id, newest.Id);

    // Same start tick: the later sequence wins, whatever the slot order.
    const AnimRequestResult tied = IssueAnimRequest(set, fx.Desc(2, "anim.intent.aim"), 7, nullptr);
    EXPECT_EQ(FindPrimaryAnimRequest(set, aim, 7)->Id, tied.Id);

    // Restricted to layers the tied record does not touch.
    AnimRequestDesc upper = fx.Desc(3, "anim.intent.aim");
    upper.Layers = 0b10;
    const AnimRequestResult upperOnly = IssueAnimRequest(set, upper, 2, nullptr);
    set.Records[2].Layers = 0b01;
    set.Records[1].Layers = 0b01;
    set.Records[0].Layers = 0b01;
    EXPECT_EQ(FindPrimaryAnimRequest(set, aim, 7, 0b10)->Id, upperOnly.Id);
}

TEST(AnimRequests, ACancelReasonIsVisibleForTheCancelTickAndTheTailIsKept)
{
    RequestFixture fx;
    AnimRequestSet set;
    const GameplayTagId reload = fx.Tag("anim.intent.reload");
    const AnimRequestResult issued = IssueAnimRequest(set, fx.Desc(0, "anim.intent.reload"), 1, nullptr);

    EXPECT_FALSE(CancelAnimRequest(set, issued.Id, AnimCancelReason::None, 4, nullptr));
    ASSERT_TRUE(CancelAnimRequest(set, issued.Id, AnimCancelReason::Interrupted, 4, nullptr));
    EXPECT_FALSE(CancelAnimRequest(set, issued.Id, AnimCancelReason::Released, 4, nullptr));

    EXPECT_EQ(FindPrimaryAnimRequest(set, reload, 4), nullptr);
    EXPECT_EQ(FindAnimCancelReason(set, reload, 4), AnimCancelReason::Interrupted);
    EXPECT_EQ(FindAnimCancelReason(set, reload, 5), AnimCancelReason::None);

    // A latch playing the cancel out keeps the record for a late joiner.
    ExtendAnimRequestTail(set, issued.Id, 9);
    PruneAnimRequests(set, 9, nullptr);
    EXPECT_EQ(Occupied(set), 1u);
    PruneAnimRequests(set, 10, nullptr);
    EXPECT_EQ(Occupied(set), 0u);
}

TEST(AnimRequests, AMalformedRequestIsRejected)
{
    RequestFixture fx;
    AnimRequestSet set;
    AnimRequestDesc noSource = fx.Desc(0, "anim.intent.a");
    noSource.Source = EntityId{};
    EXPECT_EQ(IssueAnimRequest(set, noSource, 0, nullptr).Reject, AnimRejectReason::Malformed);
    AnimRequestDesc noLayers = fx.Desc(0, "anim.intent.a");
    noLayers.Layers = 0;
    EXPECT_EQ(IssueAnimRequest(set, noLayers, 0, nullptr).Reject, AnimRejectReason::Malformed);
    EXPECT_EQ(Occupied(set), 0u);
}

TEST(AnimRequests, TheWorldFormRefusesIntentsTheRigDoesNotDeclare)
{
    RequestFixture fx;
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    DataAssetCache data;
    RegisterAnimRequestSchema(types, schemas);
    RegisterAnimRigData(types, schemas);
    fx.Entities.SetResource(AnimRigBindings{ &data, nullptr });
    (void)fx.Tag("anim.intent.reload");
    (void)fx.Tag("anim.intent.undeclared");

    const auto load = [&](std::string_view path, std::string_view type, std::string_view json) {
        const DataAssetCompileResult compiled = types.Find(type)->Compile(*JsonParse(json));
        EXPECT_TRUE(compiled.IsValid()) << compiled.Error;
        return data.Register(path, std::string(type), compiled.Value);
    };
    (void)load("asset://animation/requests.sdata", kAnimRequestSchemaType,
               R"({ "intents": [ { "intent": "anim.intent.reload", "params": [] } ] })");
    const DataAssetHandle rig = load("asset://animation/prop.rig.sdata", kAnimRigType, R"({
        "requests": "asset://animation/requests.sdata",
        "layers": [ { "name": "anim.layer.base" } ] })");

    const EntityId animated = fx.Entities.CreateEntity();
    fx.Entities.AddComponent(animated, AnimRig{ rig });
    fx.Entities.AddComponent(animated, AnimDecisionLog{});

    EXPECT_EQ(IssueAnimRequest(fx.Entities, animated, fx.Desc(0, "anim.intent.reload"), 1).Status,
              AnimRequestStatus::Accepted);
    const AnimRequestResult refused =
        IssueAnimRequest(fx.Entities, animated, fx.Desc(0, "anim.intent.undeclared"), 1);
    EXPECT_EQ(refused.Reject, AnimRejectReason::UndeclaredIntent);
    const AnimDecisionLog& log = *fx.Entities.TryGet<AnimDecisionLog>(animated);
    EXPECT_EQ(log.At(log.Size() - 1).RejectReason, AnimRejectReason::UndeclaredIntent);

    // Without a request set there is nothing to issue into.
    const EntityId bare = fx.Entities.CreateEntity();
    EXPECT_EQ(IssueAnimRequest(fx.Entities, bare, fx.Desc(0, "anim.intent.reload"), 1).Reject,
              AnimRejectReason::Malformed);
}
