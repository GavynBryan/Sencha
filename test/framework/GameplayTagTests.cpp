#include <gameplay_tags/CountedGameplayTagSet.h>
#include <gameplay_tags/GameplayTagQuery.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <gameplay_tags/GameplayTagSet.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace
{
    GameplayTagId MustRegister(GameplayTagRegistry& registry, std::string_view name)
    {
        GameplayTagError error;
        std::optional<GameplayTagId> tag = registry.RegisterTag(name, &error);
        EXPECT_TRUE(tag.has_value()) << error.Message;
        return tag.value_or(GameplayTagId{});
    }
}

TEST(GameplayTagRegistry, RegistersHierarchyAndAutoCreatesParents)
{
    GameplayTagRegistry registry;

    const GameplayTagId grounded = MustRegister(registry, "Movement.Mode.Grounded");
    const GameplayTagId movement = registry.FindTag("Movement");
    const GameplayTagId mode = registry.FindTag("Movement.Mode");

    EXPECT_TRUE(grounded.IsValid());
    EXPECT_TRUE(movement.IsValid());
    EXPECT_TRUE(mode.IsValid());
    EXPECT_EQ(registry.Size(), 3u);
    EXPECT_EQ(registry.GetName(grounded), "Movement.Mode.Grounded");
    EXPECT_EQ(registry.GetParent(grounded), mode);
    EXPECT_EQ(registry.GetParent(mode), movement);
    EXPECT_FALSE(registry.GetParent(movement).IsValid());
    EXPECT_TRUE(registry.IsDescendantOf(grounded, movement));
    EXPECT_TRUE(registry.IsDescendantOf(grounded, mode));
    EXPECT_TRUE(registry.IsDescendantOf(grounded, grounded));
    EXPECT_FALSE(registry.IsDescendantOf(movement, grounded));

    const std::vector<GameplayTagId> children = registry.GetChildren(mode);
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children.front(), grounded);
}

TEST(GameplayTagRegistry, DuplicateRegistrationReturnsExistingId)
{
    GameplayTagRegistry registry;

    const GameplayTagId first = MustRegister(registry, "Can.Jump");
    const GameplayTagId second = MustRegister(registry, "Can.Jump");

    EXPECT_EQ(first, second);
    EXPECT_EQ(registry.Size(), 2u);
}

TEST(GameplayTagRegistry, RejectsInvalidNamesWithError)
{
    GameplayTagRegistry registry;

    for (std::string_view name : { "", ".Leading", "Trailing.", "Double..Dot",
                                   "Has Space", "Bad/Slash", "Bad-Dash" })
    {
        GameplayTagError error;
        const std::optional<GameplayTagId> tag = registry.RegisterTag(name, &error);
        EXPECT_FALSE(tag.has_value()) << name;
        EXPECT_FALSE(error.Message.empty()) << name;
    }
}

TEST(GameplayTagSet, ExactMutationAndQueriesStayExact)
{
    GameplayTagRegistry registry;
    const GameplayTagId status = MustRegister(registry, "State.Status");
    const GameplayTagId stunned = MustRegister(registry, "State.Status.Stunned");
    const GameplayTagId invulnerable = MustRegister(registry, "State.Status.Invulnerable");

    GameplayTagSet set;
    EXPECT_TRUE(set.AddExact(stunned));
    EXPECT_FALSE(set.AddExact(stunned));
    EXPECT_TRUE(set.HasExact(stunned));
    EXPECT_FALSE(set.HasExact(status));
    EXPECT_TRUE(set.HasDescendantOf(registry, status));

    EXPECT_FALSE(set.RemoveExact(status));
    EXPECT_TRUE(set.HasExact(stunned));
    EXPECT_TRUE(set.RemoveExact(stunned));
    EXPECT_FALSE(set.HasExact(stunned));

    EXPECT_TRUE(set.AddExact(stunned));
    EXPECT_TRUE(set.HasAnyExact({ invulnerable, stunned }));
    EXPECT_TRUE(set.HasAllExact({ stunned }));
    EXPECT_TRUE(set.HasNoneExact({ status, invulnerable }));

    std::size_t iterated = 0;
    for (GameplayTagId tag : set)
    {
        EXPECT_EQ(tag, stunned);
        ++iterated;
    }
    EXPECT_EQ(iterated, 1u);

    set.Clear();
    EXPECT_TRUE(set.Empty());
}

TEST(CountedGameplayTagSet, OverlappingGrantsRequireEverySourceToRevoke)
{
    GameplayTagRegistry registry;
    const GameplayTagId status = MustRegister(registry, "State.Status");
    const GameplayTagId stunned = MustRegister(registry, "State.Status.Stunned");

    CountedGameplayTagSet set;
    const GameplayTagGrantHandle stunEffect =
        set.Grant(stunned, GameplayTagSource{ .Name = "StunEffect",
                                              .Owner = "AbilitySystem",
                                              .SourceId = 10 });
    const GameplayTagGrantHandle enemyGrab =
        set.Grant(stunned, GameplayTagSource{ .Name = "EnemyGrab",
                                              .Owner = "Interaction",
                                              .SourceId = 20 });

    EXPECT_TRUE(stunEffect.IsValid());
    EXPECT_TRUE(enemyGrab.IsValid());
    EXPECT_TRUE(set.HasExact(stunned));
    EXPECT_TRUE(set.HasDescendantOf(registry, status));
    EXPECT_EQ(set.GetGrantCount(stunned), 2u);

    std::vector<GameplayTagSource> sources = set.GetSources(stunned);
    ASSERT_EQ(sources.size(), 2u);
    std::vector<std::string> names;
    for (const GameplayTagSource& source : sources)
        names.push_back(source.Name);
    std::ranges::sort(names);
    EXPECT_EQ(names, (std::vector<std::string>{ "EnemyGrab", "StunEffect" }));

    EXPECT_TRUE(set.Revoke(stunEffect));
    EXPECT_FALSE(set.Revoke(stunEffect));
    EXPECT_TRUE(set.HasExact(stunned));
    EXPECT_EQ(set.GetGrantCount(stunned), 1u);

    EXPECT_FALSE(set.Revoke(GameplayTagGrantHandle{}));
    EXPECT_FALSE(set.Revoke(GameplayTagGrantHandle{ 9999 }));

    EXPECT_TRUE(set.Revoke(enemyGrab));
    EXPECT_FALSE(set.HasExact(stunned));
    EXPECT_FALSE(set.HasDescendantOf(registry, status));
    EXPECT_EQ(set.GetGrantCount(stunned), 0u);
}

TEST(GameplayTagQuery, MatchesAllAnyNoneWithExactAndHierarchicalClauses)
{
    GameplayTagRegistry registry;
    const GameplayTagId canAttack = MustRegister(registry, "Can.Attack");
    const GameplayTagId movementMode = MustRegister(registry, "Movement.Mode");
    const GameplayTagId grounded = MustRegister(registry, "Movement.Mode.Grounded");
    const GameplayTagId airborne = MustRegister(registry, "Movement.Mode.Airborne");
    const GameplayTagId stunned = MustRegister(registry, "State.Status.Stunned");

    GameplayTagSet tags;
    ASSERT_TRUE(tags.AddExact(canAttack));
    ASSERT_TRUE(tags.AddExact(grounded));

    GameplayTagQuery attackQuery;
    attackQuery
        .AddAll(canAttack)
        .AddAll(movementMode, GameplayTagMatchMode::Hierarchical)
        .AddAny(airborne)
        .AddAny(grounded)
        .AddNone(stunned);
    EXPECT_TRUE(attackQuery.Matches(tags, registry));

    GameplayTagQuery noAnyQuery;
    noAnyQuery.AddAll(canAttack).AddNone(stunned);
    EXPECT_TRUE(noAnyQuery.Matches(tags, registry));

    GameplayTagQuery exactParentDoesNotMatchChild;
    exactParentDoesNotMatchChild.AddAll(movementMode);
    EXPECT_FALSE(exactParentDoesNotMatchChild.Matches(tags, registry));

    ASSERT_TRUE(tags.AddExact(stunned));
    EXPECT_FALSE(attackQuery.Matches(tags, registry));
}

TEST(GameplayTagQuery, MatchesCountedTagSet)
{
    GameplayTagRegistry registry;
    const GameplayTagId canAttack = MustRegister(registry, "Can.Attack");
    const GameplayTagId blocked = MustRegister(registry, "Block.Ability.Attack");

    CountedGameplayTagSet tags;
    const GameplayTagGrantHandle canAttackGrant =
        tags.Grant(canAttack, GameplayTagSource{ .Name = "BaseCharacter" });

    GameplayTagQuery query;
    query.AddAll(canAttack).AddNone(blocked);
    EXPECT_TRUE(query.Matches(tags, registry));

    const GameplayTagGrantHandle blockGrant =
        tags.Grant(blocked, GameplayTagSource{ .Name = "SilenceEffect" });
    EXPECT_FALSE(query.Matches(tags, registry));

    EXPECT_TRUE(tags.Revoke(blockGrant));
    EXPECT_TRUE(query.Matches(tags, registry));
    EXPECT_TRUE(tags.Revoke(canAttackGrant));
    EXPECT_FALSE(query.Matches(tags, registry));
}

// Ids are registration order, so two processes number the same tags
// differently; the wire key is the name's, and names the same tag in both.
TEST(GameplayTagWireKey, NamesTheSameTagWhateverOrderItRegisteredIn)
{
    GameplayTagRegistry first;
    GameplayTagRegistry second;
    const GameplayTagId walkFirst = *first.RegisterTag("Anim.Walk");
    (void)first.RegisterTag("Anim.Run");
    (void)second.RegisterTag("Anim.Run");
    (void)second.RegisterTag("Anim.Idle");
    const GameplayTagId walkSecond = *second.RegisterTag("Anim.Walk");
    ASSERT_NE(walkFirst, walkSecond);

    const std::uint32_t key = first.WireKey(walkFirst);
    EXPECT_EQ(key, GameplayTagRegistry::WireKeyOf("Anim.Walk"));
    EXPECT_EQ(second.FindByWireKey(key), walkSecond);
    // Parents registered on the way have keys too.
    EXPECT_EQ(second.FindByWireKey(GameplayTagRegistry::WireKeyOf("Anim")), second.FindTag("Anim"));
    EXPECT_FALSE(second.FindByWireKey(GameplayTagRegistry::WireKeyOf("Anim.Swim")).IsValid());
    EXPECT_EQ(first.WireKey(GameplayTagId{}), 0u);
}

// Two names that hash alike make their key say nothing rather than guess.
TEST(GameplayTagWireKey, ACollidingKeyResolvesToNoTag)
{
    ASSERT_EQ(GameplayTagRegistry::WireKeyOf("Tag.c90229"), GameplayTagRegistry::WireKeyOf("Tag.c129204"));
    GameplayTagRegistry registry;
    (void)registry.RegisterTag("Tag.c90229");
    EXPECT_TRUE(registry.FindByWireKey(GameplayTagRegistry::WireKeyOf("Tag.c90229")).IsValid());
    (void)registry.RegisterTag("Tag.c129204");
    EXPECT_FALSE(registry.FindByWireKey(GameplayTagRegistry::WireKeyOf("Tag.c90229")).IsValid());
}
