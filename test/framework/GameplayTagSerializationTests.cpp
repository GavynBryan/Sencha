#include <framework/gameplay_tags/GameplayTagContainer.h>
#include <framework/gameplay_tags/GameplayTagRegistry.h>
#include <framework/gameplay_tags/GameplayTagSerialization.h>
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
    c.Grant(root, 2);
    c.Grant(dash);
    c.Grant(burning, 5);

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
    c.Grant(Register(saveReg, "Ability.Dash"));
    c.Grant(Register(saveReg, "State.Burning"));

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
