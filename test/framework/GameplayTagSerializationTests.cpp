#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <gameplay_tags/GameplayTagSerialization.h>
#include <gameplay_tags/GameplayTagContainerWire.h>
#include <net/ReplicationWireContext.h>
#include <ecs/World.h>
#include <core/serialization/JsonArchive.h>

#include <gtest/gtest.h>

namespace
{
    GameplayTagId Register(GameplayTagRegistry& reg, const char* name)
    {
        auto id = reg.RegisterTag(name);
        EXPECT_TRUE(id.has_value()) << "failed to register " << name;
        return id.value_or(GameplayTagId{});
    }
}

// The core of the Stage 1 gate: tags persist by NAME and reload against a
// registry that assigns DIFFERENT ids (different registration order), proving
// the round-trip rides on names, not on registration-order-dependent ids.
TEST(GameplayTagSerialization, RoundTripsByNameAcrossDifferingRegistries)
{
    GameplayTagRegistry saveReg;
    const GameplayTagId root    = Register(saveReg, "State.Stunned.Root");
    const GameplayTagId dash    = Register(saveReg, "Ability.Dash");
    const GameplayTagId burning = Register(saveReg, "State.Burning");

    GameplayTagContainer c{};
    c.GrantBaseline(root, 2);
    c.GrantBaseline(dash);
    c.GrantBaseline(burning, 5);

    JsonWriteArchive wa;
    ASSERT_TRUE(WriteGameplayTags(wa, c, saveReg));
    const JsonValue jv = wa.TakeValue();

    GameplayTagRegistry loadReg; // same names, different order -> different ids
    Register(loadReg, "Z.Unrelated");
    Register(loadReg, "Ability.Dash");
    Register(loadReg, "State.Burning");
    Register(loadReg, "State.Stunned.Root");
    EXPECT_NE(saveReg.FindTag("Ability.Dash").Value, loadReg.FindTag("Ability.Dash").Value);

    GameplayTagContainer c2{};
    JsonReadArchive ra(jv);
    ASSERT_TRUE(ReadGameplayTags(ra, c2, loadReg));

    EXPECT_EQ(c2.Size(), 3u);
    EXPECT_EQ(c2.StackCount(loadReg.FindTag("State.Stunned.Root")), 2u);
    EXPECT_EQ(c2.StackCount(loadReg.FindTag("Ability.Dash")), 1u);
    EXPECT_EQ(c2.StackCount(loadReg.FindTag("State.Burning")), 5u);
    EXPECT_TRUE(c2.HasDescendantOf(loadReg, loadReg.FindTag("State.Stunned")));
}

TEST(GameplayTagSerialization, UnknownTagsAreSkippedNotFatal)
{
    GameplayTagRegistry saveReg;
    GameplayTagContainer c{};
    c.GrantBaseline(Register(saveReg, "Ability.Dash"));
    c.GrantBaseline(Register(saveReg, "State.Burning"));

    JsonWriteArchive wa;
    ASSERT_TRUE(WriteGameplayTags(wa, c, saveReg));
    const JsonValue jv = wa.TakeValue();

    GameplayTagRegistry sparse; // knows only Ability.Dash; State.Burning is unknown
    Register(sparse, "Ability.Dash");

    GameplayTagContainer c2{};
    JsonReadArchive ra(jv);
    EXPECT_TRUE(ReadGameplayTags(ra, c2, sparse)); // not fatal
    EXPECT_EQ(c2.Size(), 1u);
    EXPECT_TRUE(c2.HasExact(sparse.FindTag("Ability.Dash")));
}

TEST(GameplayTagSerialization, EmptyContainerRoundTrips)
{
    GameplayTagRegistry reg;
    Register(reg, "A");

    GameplayTagContainer c{};
    JsonWriteArchive wa;
    ASSERT_TRUE(WriteGameplayTags(wa, c, reg));
    const JsonValue jv = wa.TakeValue();

    GameplayTagContainer c2{};
    JsonReadArchive ra(jv);
    EXPECT_TRUE(ReadGameplayTags(ra, c2, reg));
    EXPECT_TRUE(c2.Empty());
}

// Contributions are derived again after a load.
TEST(GameplayTagSerialization, OnlyTheBaselineIsSavedAndItLoadsAsBaseline)
{
    GameplayTagRegistry reg;
    const GameplayTagId humanoid = Register(reg, "Creature.Humanoid");
    const GameplayTagId burning = Register(reg, "State.Burning");

    GameplayTagContainer c{};
    c.GrantBaseline(humanoid);
    c.Grant(humanoid);
    c.Grant(burning, 2);
    JsonWriteArchive wa;
    ASSERT_TRUE(WriteGameplayTags(wa, c, reg));
    const JsonValue jv = wa.TakeValue();

    GameplayTagContainer loaded{};
    JsonReadArchive ra(jv);
    ASSERT_TRUE(ReadGameplayTags(ra, loaded, reg));
    EXPECT_EQ(loaded.Size(), 1u);
    EXPECT_EQ(loaded.StackCount(humanoid), 1u);
    EXPECT_EQ(loaded.BaselineCount(humanoid), 1u);
    EXPECT_FALSE(loaded.HasExact(burning));
}

// A saved stack of a projected tag cannot mean anything.
TEST(GameplayTagSerialization, AProjectedTagLoadsAsNothingAndIsReported)
{
    GameplayTagRegistry reg;
    const GameplayTagId grounded = Register(reg, "movement.grounded");
    const GameplayTagId humanoid = Register(reg, "Creature.Humanoid");
    GameplayTagContainer c{};
    c.GrantBaseline(grounded);
    c.GrantBaseline(humanoid);
    JsonWriteArchive wa;
    ASSERT_TRUE(WriteGameplayTags(wa, c, reg));
    const JsonValue jv = wa.TakeValue();

    reg.SetProjected(grounded, "movement.support");
    GameplayTagContainer loaded{};
    std::vector<DroppedGameplayTag> dropped;
    JsonReadArchive ra(jv);
    ASSERT_TRUE(ReadGameplayTags(ra, loaded, reg, &dropped));
    EXPECT_FALSE(loaded.HasExact(grounded));
    EXPECT_TRUE(loaded.HasExact(humanoid));
    EXPECT_EQ(dropped, (std::vector<DroppedGameplayTag>{ { "movement.grounded", GameplayTagDrop::Projected } }));
}

namespace
{
    JsonValue TagsJson(std::size_t count, std::uint32_t stacks = 1)
    {
        JsonWriteArchive wa;
        wa.BeginObject(std::string_view{});
        wa.BeginArray(std::string_view{ "tags" }, count);
        for (std::size_t i = 0; i < count; ++i)
        {
            wa.BeginObject(std::string_view{});
            wa.Field(std::string_view{ "name" }, std::string_view("Fill.T" + std::to_string(i)));
            wa.Field(std::string_view{ "stacks" }, stacks);
            wa.End();
        }
        wa.End();
        wa.End();
        return wa.TakeValue();
    }
}

TEST(GameplayTagSerialization, TagsPastTheContainersCapacityAreDroppedAndReported)
{
    GameplayTagRegistry reg;
    for (std::size_t i = 0; i <= GameplayTagContainer::Capacity; ++i)
        (void)Register(reg, ("Fill.T" + std::to_string(i)).c_str());
    GameplayTagContainer loaded{};
    std::vector<DroppedGameplayTag> dropped;
    const JsonValue saved = TagsJson(GameplayTagContainer::Capacity + 1u);
    JsonReadArchive ra(saved);
    ASSERT_TRUE(ReadGameplayTags(ra, loaded, reg, &dropped));
    EXPECT_EQ(loaded.Size(), GameplayTagContainer::Capacity);
    EXPECT_EQ(dropped, (std::vector<DroppedGameplayTag>{ { "Fill.T32", GameplayTagDrop::Full } }));
}

TEST(GameplayTagSerialization, StacksPastTheirRangeSaturateRatherThanVanish)
{
    GameplayTagRegistry reg;
    const GameplayTagId only = Register(reg, "Fill.T0");
    GameplayTagContainer loaded{};
    const JsonValue saved = TagsJson(1, 0x10000u);
    JsonReadArchive ra(saved);
    ASSERT_TRUE(ReadGameplayTags(ra, loaded, reg));
    EXPECT_EQ(loaded.BaselineCount(only), 0xFFFFu);
}

// A replica holds the authority's totals as contributions.
TEST(GameplayTagSerialization, AContainerDecodedFromTheWireHoldsNoBaselineAndSavesNothing)
{
    World authority;
    GameplayTagRegistry& names = authority.AddResource<GameplayTagRegistry>();
    const GameplayTagId humanoid = Register(names, "Creature.Humanoid");
    const GameplayTagId burning = Register(names, "State.Burning");
    GameplayTagContainer held{};
    held.GrantBaseline(humanoid);
    held.Grant(burning, 2);
    ReplicationLayout layout;
    ASSERT_TRUE(layout.AddCodec<GameplayTagContainer>());
    ReplicationImage wire(*layout.Find(ResolveComponentTypeId<GameplayTagContainer>()));
    ReplicationCodec<GameplayTagContainer>::ToWire(
        ReplicationWireContext{ .Entities = &authority, .Entity = {}, .Authority = nullptr, .Client = nullptr, .Unresolved = nullptr },
        held, wire);

    World client;
    GameplayTagRegistry& theirs = client.AddResource<GameplayTagRegistry>();
    (void)Register(theirs, "State.Burning");
    const GameplayTagId theirHumanoid = Register(theirs, "Creature.Humanoid");
    GameplayTagContainer decoded{};
    ReplicationCodec<GameplayTagContainer>::FromWire(
        ReplicationWireContext{ .Entities = &client, .Entity = {}, .Authority = nullptr, .Client = nullptr, .Unresolved = nullptr },
        wire, decoded);
    ASSERT_EQ(decoded.StackCount(theirHumanoid), 1u);
    for (std::uint8_t i = 0; i < decoded.Count; ++i)
        EXPECT_EQ(decoded.BaselineCount(decoded.Tags[i]), 0u);
    JsonWriteArchive written;
    ASSERT_TRUE(WriteGameplayTags(written, decoded, theirs));
    EXPECT_EQ(written.TakeValue().Find("tags")->AsArray().size(), 0u);
}
