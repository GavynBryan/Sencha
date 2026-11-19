#pragma once

#include <ecs/ComponentAnnotations.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <span>
#include <type_traits>

class GameplayTagRegistry;

enum class GameplayTagWrite : std::uint8_t
{
    Unchanged,
    Added,
    Restacked,
    Removed,
    Full,
};

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
// here are registration-order values.
//=============================================================================
struct SENCHA_COMPONENT("sencha.gameplay_tag_container") GameplayTagContainer
{
    static constexpr std::uint8_t Capacity = 32;

    GameplayTagId Tags[Capacity];
    std::uint16_t Counts[Capacity];
    // Within Counts: the stacks that persist.
    std::uint16_t Baseline[Capacity];
    std::uint8_t  Count = 0;

    // Counts saturate at 0xFFFF; a baseline stack never displaces a contributed one.
    GameplayTagWrite Grant(GameplayTagId tag, std::uint16_t stacks = 1);

    // Never takes the baseline. True iff the last stack was just removed.
    bool Revoke(GameplayTagId tag, std::uint16_t stacks = 1);

    GameplayTagWrite GrantBaseline(GameplayTagId tag, std::uint16_t stacks = 1);
    bool RevokeBaseline(GameplayTagId tag, std::uint16_t stacks = 1);

    bool Erase(GameplayTagId tag);

    GameplayTagWrite SetContribution(GameplayTagId tag, std::uint16_t stacks);

    [[nodiscard]] bool HasExact(GameplayTagId tag) const;
    [[nodiscard]] bool CanHoldAll(std::span<const GameplayTagId> tags) const;
    [[nodiscard]] std::uint16_t StackCount(GameplayTagId tag) const;
    [[nodiscard]] std::uint16_t BaselineCount(GameplayTagId tag) const;
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
