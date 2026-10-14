#pragma once

#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityId.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// One World's abilities, named by the asset path that declares them. A name is
// given an id the first time anything refers to it, so a grant read before its
// ability is declared, or one whose ability failed to compile, keeps its id and
// its name; the definition arrives, and is replaced on reload, under that id.
class AbilityRegistry
{
public:
    AbilityRegistry();

    // The id for `name`, minted if new. Invalid only for an empty name.
    AbilityId Reserve(std::string_view name);

    // Replaces whatever `name` held and keeps its id.
    AbilityId Define(std::string_view name, AbilityDefinition definition);

    // Leaves the name reserved and the ability undefined.
    void Undefine(AbilityId id);

    [[nodiscard]] AbilityId Find(std::string_view name) const;
    // Null for an unknown id or a reserved name nothing has defined.
    [[nodiscard]] const AbilityDefinition* Get(AbilityId id) const;
    [[nodiscard]] std::string_view GetName(AbilityId id) const;
    [[nodiscard]] bool IsKnown(AbilityId id) const;
    // Moves each time `id` is defined or undefined, so a running activation can
    // tell that the definition it was started from has gone.
    [[nodiscard]] std::uint32_t Revision(AbilityId id) const;
    [[nodiscard]] std::size_t Size() const;

    // Moves whenever a definition is defined or undefined.
    [[nodiscard]] std::uint64_t Generation() const { return Generation_; }

private:
    struct Entry
    {
        std::string Name;
        // Boxed, so a definition's address survives later names being reserved.
        std::unique_ptr<const AbilityDefinition> Definition;
        std::uint32_t Revision = 0;
    };

    std::vector<Entry> Abilities;
    std::unordered_map<std::string, AbilityId> IdsByName;
    std::uint64_t Generation_ = 0;
};
