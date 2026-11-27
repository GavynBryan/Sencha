#pragma once

#include <abilities/AbilityDeclaration.h>
#include <authored/AuthoredValue.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>
#include <gameplay_tags/GameplayTagQueryNames.h>
#include <inventory/ItemDefinition.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class World;

inline constexpr std::string_view kItemDefinitionType = "item.definition";

struct ItemDeclaration
{
    struct Attribute
    {
        std::string Name;
        float Base = 0.0f;
    };

    std::string Name;
    std::vector<std::string> Tags;
    std::uint32_t StackMax = 1;
    bool Keep = true;
    std::vector<std::string> TakeEffects;
    std::vector<Attribute> Attributes;
    std::vector<std::string> InstanceTags;
    std::optional<AbilityGrantDeclaration> Use;

    struct EquipEffect
    {
        std::string Effect;
        float Scale = 1.0f;
        std::string ScaleBy{};
    };

    // A tag value is held by name in `Tag` until a World resolves it.
    struct Property
    {
        std::string Name;
        AuthoredValue Value;
        std::string Tag;
    };
    std::vector<std::string> Occupies;
    std::vector<EquipEffect> EquipEffects;
    std::vector<std::string> EquipLockTags;
    GameplayTagQueryNames EquipRequire;
    std::vector<ItemGrantParameters> GrantParameters;
    std::vector<Property> Properties;
};

void RegisterItemDefinitionData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

[[nodiscard]] bool ResolveItemDeclaration(const ItemDeclaration& declaration, World& world, ItemDefinition& out,
                                          std::vector<std::string>& errors);
