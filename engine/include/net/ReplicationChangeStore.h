#pragma once

#include <core/identity/Id.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationSnapshot.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

class World;

//=============================================================================
// ReplicationChangeStore
//
// What the authority has published, and when each part of it last moved.
//
// One copy for the whole session rather than one per peer. That is the point:
// deciding what changed is a question about the world's own history and has one
// answer, so asking it once per publish costs the same at one peer as at
// sixteen. It used to be asked per peer, by comparing every component against
// that peer's own remembered bytes -- which is the same work repeated, and the
// memory to repeat it with.
//
// It also changes what a difference means. Comparing against a peer's bytes
// answers "does this differ from what they hold", which sounds equivalent and
// is not: a value that changes and changes back inside one round trip differs
// from nothing, so it is never sent, while the peer sits on the value from the
// middle of that excursion forever. Recording when a run last moved answers
// "has this moved since they last proved they were up to date", which is the
// question that has no such hole.
//
// Values are stored already snapped to wire precision, so a field that jitters
// below its own resolution is not a change and costs nothing.
//=============================================================================
class ReplicationChangeStore
{
public:
    // One component of one entity, as last published.
    struct ComponentState
    {
        std::uint8_t WireIndex = 0;
        // Wire-snapped bytes, sized to the component.
        std::vector<std::byte> Bytes;
        // One entry per field run: the generation that run last moved at.
        std::vector<std::uint64_t> ChangedAt;
        // The last pass that found this component on the entity, for the same
        // reason EntityState carries one: what a pass did not see is gone.
        std::uint64_t SeenAt = 0;
    };

    // A component an entity used to carry. Held until every peer has been told,
    // the same way a destroyed entity is: a peer that has been shown the
    // component and not shown it going keeps it forever, and a peer that joins
    // afterwards would otherwise be sent state the authority does not have.
    struct RemovedComponent
    {
        std::uint8_t WireIndex = 0;
        // When it went. A peer whose floor is below this has not been told.
        std::uint64_t RemovedAt = 0;
    };

    struct EntityState
    {
        NetEntityId Id;
        // The authored identity this entity was loaded under, when it has one.
        // Both machines load the same level and reach the same value under
        // different runtime handles, so this is what lets a client recognise an
        // entity it already has instead of building a second one beside it.
        // Invalid for anything the authority spawned at runtime.
        PersistentEntityId Persistent;
        // Ordered by wire index, so a snapshot writes components in a fixed
        // order without sorting per peer.
        std::vector<ComponentState> Components;
        // Ordered by wire index for the same reason.
        std::vector<RemovedComponent> Removed;
        // Zero when nobody owns it, which is everything the authority drives.
        std::uint32_t Owner = 0;
        // When ownership last moved. Whether a field is visible to a peer
        // depends on who owns the entity, and that is not something the field's
        // own change generation can express: a transfer makes owner-gated runs
        // newly visible to one peer and newly hidden from another without any
        // of them having changed value.
        std::uint64_t OwnerChangedAt = 0;
        // The last pass that found this entity in the world. Anything not
        // touched by a pass is gone -- read off the walk that has to happen
        // anyway, rather than by asking the world about entities it has
        // already forgotten.
        std::uint64_t SeenAt = 0;
    };

    // Walks the replicated world once, snaps every component to wire precision,
    // and stamps the runs that moved with `generation`. Generations must
    // increase; the caller owns the counter because it also names the snapshots
    // that carry the result.
    void Update(World& world, const ReplicationLayout& layout,
                ReplicationAuthorityIdentity& identity,
                std::uint64_t generation);

    // Everything alive as of the last Update, ordered by NetEntityId so two
    // runs of the same simulation produce the same snapshot bytes.
    [[nodiscard]] std::span<const EntityState> Live() const
    {
        return { LiveEntities.data(), LiveEntities.size() };
    }

    // Entities that were alive and are not any more, since the caller last
    // took them. Held rather than derived per peer: an entity's absence from
    // the world is a fact about the world, while which peers still need telling
    // is a fact about each peer.
    [[nodiscard]] std::span<const NetEntityId> Departed() const
    {
        return { DepartedEntities.data(), DepartedEntities.size() };
    }

    [[nodiscard]] const EntityState* Find(NetEntityId id) const;
    [[nodiscard]] std::uint64_t Generation() const { return LastGeneration; }
    [[nodiscard]] std::size_t Size() const { return LiveEntities.size(); }

    void Reset();

private:
    // Indexed by NetEntityId, so an entity's history survives it moving between
    // chunks or the query visiting it in a different order.
    std::unordered_map<NetEntityId, EntityState> Published;
    std::vector<EntityState> LiveEntities;
    std::vector<NetEntityId> DepartedEntities;
    std::uint64_t LastGeneration = 0;
};
