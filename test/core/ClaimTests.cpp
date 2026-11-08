// A claim admits only as many holders as it has places, a place goes with its
// holder, and content holds one through the claims.hold lease.

#include <authored/AuthoredQueryDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/ClaimVocabulary.h>
#include <logic/Claims.h>

#include <gtest/gtest.h>

namespace
{
    struct ClaimWorld
    {
        World Entities;
        GameplayTagRegistry* Tags = nullptr;
        GameplayTagId Squad;
        GameplayTagId Flank;

        ClaimWorld()
        {
            Entities.RegisterComponent<GameplayTagContainer>();
            Tags = &Entities.AddResource<GameplayTagRegistry>();
            Squad = *Tags->RegisterTag("squad.red");
            Flank = *Tags->RegisterTag("role.flank");
        }

        EntityId Member()
        {
            const EntityId entity = Entities.CreateEntity();
            GameplayTagContainer tags{};
            EXPECT_TRUE(tags.Grant(Squad));
            Entities.AddComponent<GameplayTagContainer>(entity, tags);
            return entity;
        }
    };
}

TEST(Claims, AClaimAdmitsAsManyHoldersAsItHasPlaces)
{
    ClaimWorld world;
    ClaimBoard board;
    const EntityId a = world.Member();
    const EntityId b = world.Member();
    const EntityId c = world.Member();
    EXPECT_TRUE(board.Take(world.Entities, world.Squad, world.Flank, { a, 1 }, 2));
    EXPECT_TRUE(board.Take(world.Entities, world.Squad, world.Flank, { a, 1 }, 2)) << "a holder keeps its place";
    EXPECT_TRUE(board.Take(world.Entities, world.Squad, world.Flank, { b, 1 }, 2));
    EXPECT_FALSE(board.Take(world.Entities, world.Squad, world.Flank, { c, 1 }, 2));
    EXPECT_EQ(board.Count(world.Entities, world.Squad, world.Flank), 2u);
    EXPECT_TRUE(board.Holds(world.Entities, world.Squad, world.Flank, b));

    board.Release(world.Squad, world.Flank, { b, 1 });
    EXPECT_TRUE(board.Take(world.Entities, world.Squad, world.Flank, { c, 1 }, 2));
    world.Entities.DestroyEntity(a);
    EXPECT_EQ(board.Count(world.Entities, world.Squad, world.Flank), 1u) << "a removed holder holds nothing";
    EXPECT_TRUE(board.Take(world.Entities, world.Squad, world.Flank, { b, 1 }, 2));
}

TEST(Claims, AShareOfTheGroupScalesWithItsMembers)
{
    ClaimWorld world;
    EXPECT_EQ(ClaimCapacity(1, 0.25f, 3), 1u);
    EXPECT_EQ(ClaimCapacity(1, 0.25f, 9), 2u);
    EXPECT_EQ(ClaimCapacity(0, 0.0f, 9), 0u);
    for (int i = 0; i < 9; ++i)
        (void)world.Member();
    (void)world.Entities.CreateEntity();
    EXPECT_EQ(CountGroupMembers(world.Entities, world.Squad, *world.Tags), 9u);
}

TEST(Claims, ContentHoldsAPlaceThroughTheLeaseAndLetsGo)
{
    ClaimWorld world;
    InstallAuthoredVocabulary(world.Entities);
    ASSERT_TRUE(InstallClaimVocabulary(world.Entities));
    AuthoredQueryDispatcher queries(*FindAuthoredQueryRegistry(world.Entities));
    const std::vector<AuthoredQueryBindingToken> answers = BindClaimQueries(queries, world.Entities);
    const EntityId a = world.Member();
    const EntityId b = world.Member();

    AuthoredArguments arguments(4);
    arguments.Set(0, AuthoredValue::Tag(world.Squad));
    arguments.Set(1, AuthoredValue::Tag(world.Flank));
    arguments.Set(2, AuthoredValue::Int(1));
    arguments.Set(3, AuthoredValue::Float(0.0));
    const auto invoke = [&](EntityId owner, LeaseStep step) {
        VerbInvocation call;
        call.Arguments = step == LeaseStep::Release ? nullptr : &arguments;
        call.Owner = { owner, 7 };
        call.Step = step;
        return HoldClaim(world.Entities, call);
    };
    EXPECT_EQ(invoke(a, LeaseStep::Acquire), VerbAdmission::Accepted);
    EXPECT_EQ(invoke(b, LeaseStep::Acquire), VerbAdmission::Refused) << "one place, taken";

    AuthoredValue answer;
    const std::vector<AuthoredValue> asked{ AuthoredValue::Tag(world.Squad), AuthoredValue::Tag(world.Flank) };
    ASSERT_EQ(queries.Evaluate(queries.Registry().Resolve(kClaimsCountQuery), asked, answer), AuthoredQueryStatus::Value);
    std::int64_t held = 0;
    ASSERT_TRUE(answer.TryGetInt(held));
    EXPECT_EQ(held, 1);

    EXPECT_EQ(invoke(a, LeaseStep::Release), VerbAdmission::Accepted);
    EXPECT_EQ(invoke(b, LeaseStep::Acquire), VerbAdmission::Accepted);
    const std::vector<AuthoredValue> holder{ AuthoredValue::Entity(b), AuthoredValue::Tag(world.Squad),
                                             AuthoredValue::Tag(world.Flank) };
    ASSERT_EQ(queries.Evaluate(queries.Registry().Resolve(kClaimsHeldQuery), holder, answer), AuthoredQueryStatus::Value);
    bool holds = false;
    ASSERT_TRUE(answer.TryGetBool(holds));
    EXPECT_TRUE(holds);
}
