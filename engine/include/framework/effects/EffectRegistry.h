#pragma once

#include <framework/effects/EffectDefinition.h>
#include <framework/effects/EffectId.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

//=============================================================================
// EffectRegistry
//
// Authored effect definitions keyed by name/id, installed as a world resource —
// the same data-driven shape as AttributeRegistry / GameplayTagRegistry. (An
// EffectId can be backed by a loaded asset; the indirection is already in place.)
//=============================================================================
class EffectRegistry
{
public:
    EffectRegistry();

    // Registers a definition by name. Re-registering an existing name returns the
    // existing id and keeps the first definition. Invalid id for an empty name.
    EffectId Register(std::string_view name, EffectDefinition definition);

    [[nodiscard]] EffectId Find(std::string_view name) const;
    [[nodiscard]] const EffectDefinition* Get(EffectId id) const; // nullptr if unknown
    [[nodiscard]] std::string_view GetName(EffectId id) const;
    [[nodiscard]] bool IsKnown(EffectId id) const;
    [[nodiscard]] std::size_t Size() const;

private:
    struct Entry
    {
        std::string Name;
        EffectDefinition Def;
    };

    std::vector<Entry> Effects;                          // [0] dummy: id 0 invalid
    std::unordered_map<std::string, EffectId> IdsByName;
};
