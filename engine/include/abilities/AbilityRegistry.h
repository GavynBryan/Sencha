#pragma once

#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityId.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

//=============================================================================
// AbilityRegistry
//
// Authored ability definitions keyed by name/id, installed as a world resource —
// the same data-driven shape as Effect/Attribute/GameplayTag registries.
//=============================================================================
class AbilityRegistry
{
public:
    AbilityRegistry();

    // Registers a definition by name. Re-registering an existing name returns the
    // existing id and keeps the first definition. Invalid id for an empty name.
    AbilityId Register(std::string_view name, AbilityDefinition definition);

    [[nodiscard]] AbilityId Find(std::string_view name) const;
    [[nodiscard]] const AbilityDefinition* Get(AbilityId id) const; // nullptr if unknown
    [[nodiscard]] std::string_view GetName(AbilityId id) const;
    [[nodiscard]] bool IsKnown(AbilityId id) const;
    [[nodiscard]] std::size_t Size() const;

private:
    struct Entry
    {
        std::string Name;
        AbilityDefinition Def;
    };

    std::vector<Entry> Abilities;                          // [0] dummy: id 0 invalid
    std::unordered_map<std::string, AbilityId> IdsByName;
};
