// The editing rules behind the inspector rows for the components whose values
// are registry-resolved names. No ImGui, no document, no world: the rules are
// plain functions over a component and its registry, which is the point of
// their living outside the draw call.

#include "authoring/GameplayVocabularyEdits.h"

#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySet.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/LocomotionMode.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/CharacterMovement.h>

#include <gtest/gtest.h>

#include <string>

TEST(GameplayVocabularyEdits, AnUnknownTagNameIsDeclaredRatherThanDropped)
{
    GameplayTagRegistry registry;
    GameplayTagContainer tags{};

    const VocabularyEdit granted = GrantTagByName(tags, registry, "State.Stunned");
    EXPECT_TRUE(granted.Changed);
    EXPECT_TRUE(granted.Error.empty());

    const GameplayTagId id = registry.FindTag("State.Stunned");
    ASSERT_TRUE(id.IsValid());
    EXPECT_TRUE(tags.HasExact(id));
    // Registration creates the parents, which is what makes a hierarchical
    // query for "State" match this entity.
    EXPECT_TRUE(registry.FindTag("State").IsValid());
}

TEST(GameplayVocabularyEdits, ARefusedTagSaysWhy)
{
    GameplayTagRegistry registry;
    GameplayTagContainer tags{};

    EXPECT_FALSE(GrantTagByName(tags, registry, "").Changed);
    EXPECT_FALSE(GrantTagByName(tags, registry, "").Error.empty());
    EXPECT_FALSE(GrantTagByName(tags, registry, "State.Stunned", 0).Changed);

    // A malformed path is the registry's own refusal, forwarded verbatim.
    const VocabularyEdit malformed = GrantTagByName(tags, registry, "State..Stunned");
    EXPECT_FALSE(malformed.Changed);
    EXPECT_FALSE(malformed.Error.empty());

    ASSERT_TRUE(GrantTagByName(tags, registry, "State.Stunned").Changed);
    const VocabularyEdit again = GrantTagByName(tags, registry, "State.Stunned");
    EXPECT_FALSE(again.Changed);
    EXPECT_FALSE(again.Error.empty());
}

TEST(GameplayVocabularyEdits, AFullTagContainerRefusesRatherThanSilentlyDropping)
{
    GameplayTagRegistry registry;
    GameplayTagContainer tags{};
    for (int i = 0; i < GameplayTagContainer::Capacity; ++i)
        ASSERT_TRUE(GrantTagByName(tags, registry, "Fill." + std::to_string(i)).Changed);

    const VocabularyEdit overflow = GrantTagByName(tags, registry, "One.Too.Many");
    EXPECT_FALSE(overflow.Changed);
    EXPECT_FALSE(overflow.Error.empty());
    EXPECT_EQ(tags.Size(), GameplayTagContainer::Capacity);
}

TEST(GameplayVocabularyEdits, StacksAreSetOutrightRatherThanAddedTo)
{
    GameplayTagRegistry registry;
    GameplayTagContainer tags{};
    ASSERT_TRUE(GrantTagByName(tags, registry, "State.Burning", 3).Changed);
    const GameplayTagId id = registry.FindTag("State.Burning");

    EXPECT_TRUE(SetTagStacks(tags, id, 5).Changed);
    EXPECT_EQ(tags.StackCount(id), 5u);
    EXPECT_TRUE(SetTagStacks(tags, id, 1).Changed);
    EXPECT_EQ(tags.StackCount(id), 1u);

    EXPECT_FALSE(SetTagStacks(tags, id, 1).Changed); // already there
    EXPECT_TRUE(SetTagStacks(tags, id, 0).Changed);  // zero stacks is no tag
    EXPECT_FALSE(tags.HasExact(id));

    EXPECT_FALSE(SetTagStacks(tags, id, 2).Changed);
    EXPECT_FALSE(SetTagStacks(tags, id, 2).Error.empty());
}

TEST(GameplayVocabularyEdits, RevokingATagDropsEveryStackAtOnce)
{
    GameplayTagRegistry registry;
    GameplayTagContainer tags{};
    ASSERT_TRUE(GrantTagByName(tags, registry, "State.Burning", 4).Changed);
    const GameplayTagId id = registry.FindTag("State.Burning");

    EXPECT_TRUE(RevokeTag(tags, id).Changed);
    EXPECT_FALSE(tags.HasExact(id));
    EXPECT_FALSE(RevokeTag(tags, id).Changed);
}

TEST(GameplayVocabularyEdits, AnAddedAttributeStartsAtItsRegisteredDefault)
{
    AttributeRegistry registry;
    const AttributeId health = registry.RegisterAttribute("Health", 0.0f, 100.0f, 75.0f);
    AttributeSet set{};

    ASSERT_TRUE(AddAttribute(set, registry, health).Changed);
    EXPECT_FLOAT_EQ(set.GetBase(health), 75.0f);

    EXPECT_FALSE(AddAttribute(set, registry, health).Changed); // already present
    EXPECT_FALSE(AddAttribute(set, registry, AttributeId{ 99 }).Changed);
    EXPECT_FALSE(AddAttribute(set, registry, AttributeId{ 99 }).Error.empty());
}

TEST(GameplayVocabularyEdits, AnAttributeBaseClampsToItsRegisteredRange)
{
    AttributeRegistry registry;
    const AttributeId health = registry.RegisterAttribute("Health", 0.0f, 100.0f, 50.0f);
    AttributeSet set{};
    ASSERT_TRUE(AddAttribute(set, registry, health).Changed);

    EXPECT_TRUE(SetAttributeBase(set, registry, health, 250.0f).Changed);
    EXPECT_FLOAT_EQ(set.GetBase(health), 100.0f);
    EXPECT_TRUE(SetAttributeBase(set, registry, health, -20.0f).Changed);
    EXPECT_FLOAT_EQ(set.GetBase(health), 0.0f);
    EXPECT_TRUE(SetAttributeBase(set, registry, health, 42.0f).Changed);
    EXPECT_FLOAT_EQ(set.GetBase(health), 42.0f);

    // Asking for a value that clamps to what is already stored is not an edit,
    // so a drag pinned at the end of the range records nothing.
    EXPECT_FALSE(SetAttributeBase(set, registry, health, 42.0f).Changed);
    ASSERT_TRUE(SetAttributeBase(set, registry, health, 100.0f).Changed);
    EXPECT_FALSE(SetAttributeBase(set, registry, health, 200.0f).Changed);
    EXPECT_FLOAT_EQ(set.GetBase(health), 100.0f);

    const AttributeId absent = registry.RegisterAttribute("Poise");
    EXPECT_FALSE(SetAttributeBase(set, registry, absent, 1.0f).Changed);
    EXPECT_FALSE(SetAttributeBase(set, registry, absent, 1.0f).Error.empty());
}

TEST(GameplayVocabularyEdits, RemovingAnAttributeIsIdempotent)
{
    AttributeRegistry registry;
    const AttributeId health = registry.RegisterAttribute("Health");
    AttributeSet set{};
    ASSERT_TRUE(AddAttribute(set, registry, health).Changed);

    EXPECT_TRUE(RemoveAttribute(set, health).Changed);
    EXPECT_FALSE(RemoveAttribute(set, health).Changed);
    EXPECT_TRUE(set.Empty());
}

TEST(GameplayVocabularyEdits, AbilitiesAreGrantedOnlyOnceAndOnlyIfRegistered)
{
    AbilityRegistry registry;
    const AbilityId dash = registry.Register("Dash", AbilityDefinition{});
    AbilitySet set{};

    EXPECT_TRUE(GrantAbility(set, registry, dash).Changed);
    EXPECT_TRUE(set.Has(dash));

    EXPECT_FALSE(GrantAbility(set, registry, dash).Changed);
    EXPECT_FALSE(GrantAbility(set, registry, dash).Error.empty());
    EXPECT_FALSE(GrantAbility(set, registry, AbilityId{ 99 }).Changed);

    EXPECT_TRUE(RevokeAbility(set, dash).Changed);
    EXPECT_FALSE(RevokeAbility(set, dash).Changed);
}

TEST(GameplayVocabularyEdits, AFullAbilitySetRefusesRatherThanSilentlyDropping)
{
    AbilityRegistry registry;
    AbilitySet set{};
    for (int i = 0; i < AbilitySet::Capacity; ++i)
    {
        const AbilityId id = registry.Register("Fill" + std::to_string(i),
                                               AbilityDefinition{});
        ASSERT_TRUE(GrantAbility(set, registry, id).Changed);
    }

    const AbilityId extra = registry.Register("Extra", AbilityDefinition{});
    const VocabularyEdit overflow = GrantAbility(set, registry, extra);
    EXPECT_FALSE(overflow.Changed);
    EXPECT_FALSE(overflow.Error.empty());
    EXPECT_EQ(set.Size(), AbilitySet::Capacity);
}

TEST(GameplayVocabularyEdits, AnUnknownLocomotionModeNameIsTheFreeMode)
{
    GameplayTagRegistry tags;
    LocomotionModeRegistry modes(tags);
    const LocomotionModeId free = modes.RegisterFree();
    const LocomotionModeId climbing = modes.Register<KinematicState>("movement.mode.climb");

    CharacterMovement movement{};
    ASSERT_TRUE(SetLocomotionMode(movement, modes, "movement.mode.climb").Changed);
    EXPECT_EQ(movement.Mode, climbing);

    // Not this process's vocabulary, and no mode at all, mean the same thing --
    // which is what an absent mode already means in a scene.
    EXPECT_TRUE(SetLocomotionMode(movement, modes, "movement.mode.hoverboard").Changed);
    EXPECT_EQ(movement.Mode, free);
    EXPECT_FALSE(SetLocomotionMode(movement, modes, "").Changed);
    EXPECT_EQ(movement.Mode, free);
}
