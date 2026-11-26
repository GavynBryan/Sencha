#pragma once

#include <abilities/AbilityDeclaration.h>
#include <abilities/AbilityGrants.h>
#include <attributes/AttributeId.h>
#include <authored/AuthoredValue.h>
#include <core/identity/NamedDefinitionRegistry.h>
#include <core/identity/StrongId.h>
#include <core/metadata/NamedIdField.h>
#include <effects/EffectId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <gameplay_tags/GameplayTagQuery.h>

#include <cstdint>
#include <string>
#include <vector>

using ItemId = StrongId<struct ItemIdTag, std::uint32_t>;

template <>
struct NamedIdField<ItemId>
{
    static constexpr NamedIdKind Kind = NamedIdKind::DataDefinition;
    static const NamedIdVocabulary& Vocabulary();
};

struct ItemAttributeSeed
{
    AttributeId Attribute;
    float Base = 0.0f;
};

struct ItemEquipEffect
{
    EffectId Effect;
    float Scale = 1.0f;
};

struct ItemGrantParameters
{
    std::string Key;
    std::vector<AbilityGrantParameter> Parameters;

    friend bool operator==(const ItemGrantParameters&, const ItemGrantParameters&) = default;
};

struct ItemProperty
{
    GameplayTagId Name;
    AuthoredValue Value;
};

struct ItemDefinition
{
    std::string Name{};
    std::vector<GameplayTagId> Tags{};
    std::uint32_t StackMax = 1;
    bool Keep = true;
    std::vector<EffectId> TakeEffects{};
    std::vector<ItemAttributeSeed> Attributes{};
    std::vector<GameplayTagId> InstanceTags{};
    std::uint16_t UseEntry = kAbilityNoGrantEntry;
    std::vector<std::string> Occupies{};
    std::vector<ItemEquipEffect> EquipEffects{};
    std::vector<GameplayTagId> EquipLockTags{};
    std::vector<ItemGrantParameters> GrantParameters{};
    GameplayTagQuery EquipRequire{};
    bool HasEquipRequire = false;
    std::vector<ItemProperty> Properties{};
};

class ItemRegistry final : public NamedDefinitionRegistry<ItemId, ItemDefinition>
{
};
