#pragma once

#include <ecs/ComponentAnnotations.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <type_traits>

class GameplayTagRegistry;

//=============================================================================
// GameplayTagContainer
//
// Per-entity gameplay tags as a trivially-copyable ECS component: a fixed-cap,
// id-sorted set with per-tag stack counts, so a tag granted by N sources stays
// present until all N revoke it. This is the ECS-storable counterpart to
// GameplayTagSet / CountedGameplayTagSet, which are heap-backed (unordered_set /
// unordered_map) and belong to world/global state, not to chunks.
//
// Mutate it through World::TryGet<GameplayTagContainer>(entity): mutable access
// bumps the column version, so Changed<GameplayTagContainer> sees grant/revoke.
// Hierarchical queries take the GameplayTagRegistry, which lives as a world
// resource (the provenance "who granted this tag" is not stored here — the
// effect entity that granted a tag is its provenance).
//
// Scene persistence goes through a hand-written serializer
// (GameplayTagContainerSerializer) rather than a schema, because the ids held
// here are registration-order values. It saves each tag's baseline alone
// (docs/gameplay/abilitykit.md, D-A).
//=============================================================================
struct SENCHA_COMPONENT("sencha.gameplay_tag_container") GameplayTagContainer
{
    static constexpr std::uint8_t Capacity = 32;

    GameplayTagId Tags[Capacity];
    // Every stack held, baseline included.
    std::uint16_t Counts[Capacity];
    // Of Counts, the stacks that persist.
    std::uint16_t Baseline[Capacity];
    std::uint8_t  Count = 0;

    // Contributes `stacks` of `tag`. True iff the tag was absent and is now
    // present; false for another stack, an invalid tag, zero stacks, or a full
    // container (asserts in debug).
    bool Grant(GameplayTagId tag, std::uint16_t stacks = 1);

    // Withdraws up to `stacks` contributed stacks of `tag`; never the baseline.
    // Returns true iff the tag was present and the last stack was just removed.
    bool Revoke(GameplayTagId tag, std::uint16_t stacks = 1);

    // Grant and Revoke for the stacks that persist.
    bool GrantBaseline(GameplayTagId tag, std::uint16_t stacks = 1);
    bool RevokeBaseline(GameplayTagId tag, std::uint16_t stacks = 1);

    // Removes `tag` whatever holds it: a one-tick mailbox consumed.
    bool Erase(GameplayTagId tag);

    // Makes exactly `stacks` of `tag` contributed, never touching the baseline:
    // how a projection writes its tags. True iff anything changed.
    bool SetContribution(GameplayTagId tag, std::uint16_t stacks);

    [[nodiscard]] bool HasExact(GameplayTagId tag) const;
    [[nodiscard]] std::uint16_t StackCount(GameplayTagId tag) const;
    [[nodiscard]] std::uint16_t BaselineCount(GameplayTagId tag) const;
    // Of StackCount, the stacks above the baseline.
    [[nodiscard]] std::uint16_t ContributedCount(GameplayTagId tag) const;

    // True iff any held tag is `ancestor` or a descendant of it (holding
    // State.Stunned.Root satisfies a query for State.Stunned).
    [[nodiscard]] bool HasDescendantOf(const GameplayTagRegistry& registry,
                                       GameplayTagId ancestor) const;

    void Clear() { Count = 0; }

    [[nodiscard]] std::uint8_t Size() const { return Count; }
    [[nodiscard]] bool Empty() const { return Count == 0; }
};

static_assert(std::is_trivially_copyable_v<GameplayTagContainer>,
              "GameplayTagContainer must be trivially copyable to live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <gameplay_tags/GameplayTagContainer.sencha.h>
#  include <gameplay_tags/GameplayTagContainerWire.h>
#endif
