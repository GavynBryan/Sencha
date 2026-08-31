#include "GameplayVocabularyEdits.h"

#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySet.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementComponents.h>

#include <algorithm>
#include <optional>
#include <string>

namespace
{
    VocabularyEdit Refused(std::string message)
    {
        return VocabularyEdit{ false, std::move(message) };
    }

    VocabularyEdit Applied()   { return VocabularyEdit{ true, {} }; }
    VocabularyEdit Unchanged() { return VocabularyEdit{ false, {} }; }
}

VocabularyEdit GrantTagByName(GameplayTagContainer& tags,
                              GameplayTagRegistry& registry,
                              std::string_view name,
                              std::uint16_t stacks)
{
    if (name.empty())
        return Refused("A tag needs a name.");
    if (stacks == 0)
        return Refused("A tag needs at least one stack.");

    GameplayTagError error;
    const std::optional<GameplayTagId> id = registry.RegisterTag(name, &error);
    if (!id.has_value())
        return Refused(error.Message);

    if (tags.HasExact(*id))
        return Refused("This entity already carries that tag.");
    if (tags.Size() >= GameplayTagContainer::Capacity)
        return Refused("An entity holds at most "
                       + std::to_string(GameplayTagContainer::Capacity) + " tags.");

    tags.Grant(*id, stacks);
    return Applied();
}

VocabularyEdit SetTagStacks(GameplayTagContainer& tags,
                            GameplayTagId tag,
                            std::uint16_t stacks)
{
    const std::uint16_t held = tags.StackCount(tag);
    if (held == 0)
        return Refused("This entity does not carry that tag.");
    if (held == stacks)
        return Unchanged();

    // Grant adds stacks rather than setting them, so a set is the revoke of
    // what is held followed by the grant of what was asked for.
    tags.Revoke(tag, held);
    if (stacks > 0)
        tags.Grant(tag, stacks);
    return Applied();
}

VocabularyEdit RevokeTag(GameplayTagContainer& tags, GameplayTagId tag)
{
    const std::uint16_t held = tags.StackCount(tag);
    if (held == 0)
        return Unchanged();

    tags.Revoke(tag, held);
    return Applied();
}

VocabularyEdit AddAttribute(AttributeSet& set,
                            const AttributeRegistry& registry,
                            AttributeId id)
{
    if (!registry.IsKnown(id))
        return Refused("No attribute with that id is registered here.");
    if (set.Has(id))
        return Refused("This entity already carries that attribute.");
    if (set.Size() >= AttributeSet::Capacity)
        return Refused("An entity holds at most "
                       + std::to_string(AttributeSet::Capacity) + " attributes.");

    set.Add(id, registry.DefaultBase(id));
    return Applied();
}

VocabularyEdit SetAttributeBase(AttributeSet& set,
                                const AttributeRegistry& registry,
                                AttributeId id,
                                float base)
{
    if (!set.Has(id))
        return Refused("This entity does not carry that attribute.");

    const float clamped = registry.Clamp(id, base);
    if (set.GetBase(id) == clamped)
        return Unchanged();

    set.SetBase(id, clamped);
    return Applied();
}

VocabularyEdit RemoveAttribute(AttributeSet& set, AttributeId id)
{
    return set.Remove(id) ? Applied() : Unchanged();
}

VocabularyEdit GrantAbility(AbilitySet& set,
                            const AbilityRegistry& registry,
                            AbilityId id)
{
    if (!registry.IsKnown(id))
        return Refused("No ability with that id is registered here.");
    if (set.Has(id))
        return Refused("This entity already carries that ability.");
    if (set.Size() >= AbilitySet::Capacity)
        return Refused("An entity holds at most "
                       + std::to_string(AbilitySet::Capacity) + " abilities.");

    set.Grant(id);
    return Applied();
}

VocabularyEdit RevokeAbility(AbilitySet& set, AbilityId id)
{
    return set.Revoke(id) ? Applied() : Unchanged();
}

VocabularyEdit SetLocomotionMode(CharacterMovement& movement,
                                 const LocomotionModeRegistry& modes,
                                 std::string_view name)
{
    const LocomotionModeEntry* named = name.empty() ? nullptr : modes.Find(name);
    const LocomotionModeId next = named != nullptr ? named->Id : modes.FreeMode();
    if (movement.Mode == next)
        return Unchanged();

    movement.Mode = next;
    return Applied();
}
