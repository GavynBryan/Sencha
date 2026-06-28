#pragma once

#include <ecs/ComponentTypeId.h>
#include <framework/gameplay_tags/GameplayTagId.h>

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
//=============================================================================
struct GameplayTagContainer
{
    static constexpr std::uint8_t Capacity = 32;

    GameplayTagId Tags[Capacity];
    std::uint16_t Counts[Capacity];
    std::uint8_t  Count = 0;

    // Grant `stacks` of `tag`. Returns true iff the tag was absent and is now
    // present (a newly-appearing tag). Returns false on a no-op: an additional
    // stack of a tag already present, an invalid tag, stacks == 0, or a full
    // container faced with a new tag (asserts in debug).
    bool Grant(GameplayTagId tag, std::uint16_t stacks = 1);

    // Revoke `stacks` of `tag`. Returns true iff the tag was present and the
    // last stack was just removed (a newly-disappearing tag).
    bool Revoke(GameplayTagId tag, std::uint16_t stacks = 1);

    [[nodiscard]] bool HasExact(GameplayTagId tag) const;
    [[nodiscard]] std::uint16_t StackCount(GameplayTagId tag) const;

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

// Runtime-only ECS identity: scene persistence goes through the framework's
// IComponentSerializer (GameplayTagContainerSerializer), not a TypeSchema, so an
// explicit key supplies the stable component identity the World resolves.
SENCHA_DECLARE_COMPONENT_TYPE(GameplayTagContainer, "sencha.gameplay_tag_container");
