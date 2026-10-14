#pragma once

#include <abilities/AbilityDeclaration.h>
#include <abilities/AbilityId.h>
#include <gameplay_tags/GameplayTagQuery.h>

#include <cstdint>
#include <string>
#include <vector>

class AbilityRegistry;
class GameplayTagRegistry;
class InputActionRegistry;

// One authored grant: which ability, under which stable key, and what of its
// binding the grant overrides. A prefab spawned sixteen times interns one entry.
struct AbilityGrantEntry
{
    std::string Key;
    AbilityId Ability;
    AbilityBindingDecl Override;

    friend bool operator==(const AbilityGrantEntry&, const AbilityGrantEntry&) = default;
};

inline constexpr std::uint8_t kAbilityUnboundAction = 0xFF;

// An entry's binding with its ability's default filled in under every field the
// grant left out, and the names resolved to this World's ids.
struct AbilityEffectiveBinding
{
    // The action's dense index, as input edges carry it; kAbilityUnboundAction when
    // the ability is reached only by request.
    std::uint8_t Action = kAbilityUnboundAction;
    AbilityInputPolicy Policy = AbilityInputPolicy::Press;
    std::int32_t Priority = 0;
    AbilityConsume Consume = AbilityConsume::Exclusive;
    AbilityWhenBlocked WhenBlocked = AbilityWhenBlocked::Consume;
    GameplayTagQuery Context;
    bool HasContext = false;
    AbilityInterrupt OnInterrupt = AbilityInterrupt::Cancel;
};

// The World's grant entries and their effective bindings. Bindings rebuild when the
// abilities or the action vocabulary they were resolved against change.
class AbilityGrantEntries
{
public:
    // The index of an entry equal to `entry`, adding it if new. kAbilityNoGrantEntry
    // when the table is full.
    std::uint16_t Intern(AbilityGrantEntry entry);

    [[nodiscard]] const AbilityGrantEntry* Find(std::uint16_t index) const;
    [[nodiscard]] const AbilityEffectiveBinding* Binding(std::uint16_t index) const;
    [[nodiscard]] std::size_t Size() const { return Entries.size(); }

    // Re-resolves every binding when anything it was resolved against moved. A name
    // that does not resolve leaves that binding unbound and is reported once per
    // rebuild.
    void Refresh(const AbilityRegistry& abilities, const InputActionRegistry* actions,
                 std::uint64_t actionsRevision, const GameplayTagRegistry& tags, std::vector<std::string>& problems);

private:
    struct Slot
    {
        AbilityGrantEntry Entry;
        AbilityEffectiveBinding Binding;
    };

    std::vector<Slot> Entries;
    bool Stale = true;
    std::uint64_t AbilitiesGeneration = 0;
    const InputActionRegistry* Actions = nullptr;
    std::uint64_t ActionsRevision = 0;
};

[[nodiscard]] GameplayTagQuery ResolveTagQuery(const AbilityTagQueryDecl& declared, const GameplayTagRegistry& tags,
                                               std::vector<std::string>& missing);
