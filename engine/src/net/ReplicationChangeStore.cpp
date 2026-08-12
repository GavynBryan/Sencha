#include <net/ReplicationChangeStore.h>

#include <ecs/Query.h>
#include <ecs/World.h>
#include <net/NetReplicationComponents.h>
#include <net/ReplicationCodec.h>
#include <world/identity/PersistentIdComponent.h>

#include <algorithm>
#include <cassert>
#include <cstring>

const ReplicationChangeStore::EntityState* ReplicationChangeStore::Find(
    NetEntityId id) const
{
    const auto it = Published.find(id);
    return it == Published.end() ? nullptr : &it->second;
}

void ReplicationChangeStore::Reset()
{
    Published.clear();
    LiveEntities.clear();
    DepartedEntities.clear();
    LastGeneration = 0;
}

void ReplicationChangeStore::Update(World& world, const ReplicationLayout& layout,
                                    ReplicationAuthorityIdentity& identity,
                                    std::uint64_t generation)
{
    assert(generation > LastGeneration
           && "Change generations must increase; a repeat would hide movement.");
    LastGeneration = generation;

    LiveEntities.clear();
    DepartedEntities.clear();

    // Nothing is marked for replication in a world that never registered the
    // marker, which is every single-player world.
    if (!world.IsRegistered(ResolveComponentTypeId<NetReplicated>()))
    {
        for (const auto& [id, state] : Published)
            DepartedEntities.push_back(id);
        Published.clear();
        std::sort(DepartedEntities.begin(), DepartedEntities.end(),
                  [](NetEntityId a, NetEntityId b) { return a.Value < b.Value; });
        return;
    }

    // Which component sits in which column, resolved once rather than per
    // entity: a ComponentTypeId lookup is a hash probe and this is the hot loop.
    struct ResolvedComponent
    {
        std::uint8_t WireIndex;
        ComponentId Column;
        const ReplicatedComponent* Layout;
    };
    std::vector<ResolvedComponent> columns;
    columns.reserve(layout.Size());
    for (std::size_t i = 0; i < layout.Size(); ++i)
    {
        const ReplicatedComponent* component = layout.At(static_cast<std::uint8_t>(i));
        if (!world.IsRegistered(component->Type))
            continue;  // The authority does not store it; nothing to publish.
        columns.push_back(ResolvedComponent{
            .WireIndex = static_cast<std::uint8_t>(i),
            .Column = world.GetComponentIdByType(component->Type),
            .Layout = component,
        });
    }

    const World& reading = world;
    const bool hasOwners = world.IsRegistered(ResolveComponentTypeId<NetOwner>());
    const bool hasPersistentIds =
        world.IsRegistered(ResolveComponentTypeId<PersistentIdComponent>());

    // Reused for every component of every entity. Most components have not
    // moved on most publishes, and that case must not pay an allocation just to
    // find out.
    std::vector<std::byte> scratch;

    // A const walk: this reads the world without publishing a write, so
    // running it cannot make everything look changed on the next tick.
    Query<With<NetReplicated>> replicated(world);
    replicated.ForEachChunk([&](auto& view) {
        for (std::uint32_t row = 0; row < view.Count(); ++row)
        {
            const EntityId entity = view.Entity(row);
            const NetEntityId id = identity.IdFor(entity);

            EntityState& state = Published[id];
            state.Id = id;
            state.SeenAt = generation;

            // Read every pass rather than once: an entity's authored identity
            // does not change, but which entity a NetEntityId names can, and
            // the store outlives neither.
            if (hasPersistentIds)
            {
                if (const PersistentIdComponent* authored =
                        reading.TryGet<PersistentIdComponent>(entity))
                {
                    state.Persistent = authored->Id;
                }
            }

            std::uint32_t owner = 0;
            if (hasOwners)
            {
                if (const NetOwner* held = reading.TryGet<NetOwner>(entity))
                    owner = held->Peer;
            }
            if (owner != state.Owner)
            {
                state.Owner = owner;
                state.OwnerChangedAt = generation;
            }

            for (const ResolvedComponent& column : columns)
            {
                if (!reading.HasComponent(entity, column.Column))
                    continue;
                const void* raw = reading.GetComponentRaw(entity, column.Column);
                if (raw == nullptr)
                    continue;

                const std::size_t size = column.Layout->Size;
                const std::size_t runs = column.Layout->Fields.size();

                // Snapped before it is compared or stored, so what is recorded
                // is exactly what a peer will hold. Comparing raw values would
                // call movement finer than the wire can express a change.
                const auto* rawBytes = static_cast<const std::byte*>(raw);
                scratch.assign(rawBytes, rawBytes + size);
                ReplicationSnapToWire(*column.Layout, scratch);

                const auto existing = std::find_if(
                    state.Components.begin(), state.Components.end(),
                    [&](const ComponentState& held) {
                        return held.WireIndex == column.WireIndex;
                    });

                if (existing == state.Components.end())
                {
                    // First sight: everything about it is new. If the entity
                    // had this component before and lost it, the news that it
                    // went is superseded by it being back.
                    std::erase_if(state.Removed,
                                  [&](const RemovedComponent& gone) {
                                      return gone.WireIndex == column.WireIndex;
                                  });
                    ComponentState added;
                    added.WireIndex = column.WireIndex;
                    added.Bytes.assign(scratch.begin(), scratch.end());
                    added.ChangedAt.assign(runs, generation);
                    added.SeenAt = generation;
                    state.Components.push_back(std::move(added));
                    continue;
                }

                existing->SeenAt = generation;

                // A whole-component compare first, because most components do
                // not move on most publishes and this settles them in one pass
                // rather than one per run.
                if (existing->Bytes.size() == size
                    && std::memcmp(existing->Bytes.data(), scratch.data(), size) == 0)
                {
                    continue;
                }

                const std::uint64_t moved = ReplicationChangedFields(
                    *column.Layout, scratch, existing->Bytes);
                existing->ChangedAt.resize(runs, generation);
                for (std::size_t run = 0; run < runs; ++run)
                {
                    if ((moved & (std::uint64_t{ 1 } << run)) != 0)
                        existing->ChangedAt[run] = generation;
                }
                existing->Bytes.assign(scratch.begin(), scratch.end());
            }

            // A component the walk did not find is one the entity no longer
            // has. Recorded as removed rather than simply dropped: dropping it
            // leaves every peer that was shown it holding it forever, and
            // leaves a peer joining later being sent it, because a fresh floor
            // owes everything the store still holds.
            for (auto it = state.Components.begin(); it != state.Components.end();)
            {
                if (it->SeenAt == generation)
                {
                    ++it;
                    continue;
                }
                state.Removed.push_back(
                    RemovedComponent{ .WireIndex = it->WireIndex,
                                      .RemovedAt = generation });
                it = state.Components.erase(it);
            }

            // Ordered so a snapshot writes components in a fixed order.
            std::sort(state.Components.begin(), state.Components.end(),
                      [](const ComponentState& a, const ComponentState& b) {
                          return a.WireIndex < b.WireIndex;
                      });
            std::sort(state.Removed.begin(), state.Removed.end(),
                      [](const RemovedComponent& a, const RemovedComponent& b) {
                          return a.WireIndex < b.WireIndex;
                      });
        }
    });

    // What survived this pass, and what did not. An entity is gone when the
    // world no longer has it, which is a fact about the world -- who still
    // needs telling is each peer's own business.
    LiveEntities.reserve(Published.size());
    for (const auto& [id, state] : Published)
    {
        if (state.SeenAt == generation)
            LiveEntities.push_back(state);
        else
            DepartedEntities.push_back(id);
    }

    for (NetEntityId id : DepartedEntities)
        Published.erase(id);

    // Deterministic order: an unordered_map's iteration order is not a
    // contract, and two runs of the same simulation must produce the same
    // bytes.
    std::sort(LiveEntities.begin(), LiveEntities.end(),
              [](const EntityState& a, const EntityState& b) {
                  return a.Id.Value < b.Id.Value;
              });
    std::sort(DepartedEntities.begin(), DepartedEntities.end(),
              [](NetEntityId a, NetEntityId b) { return a.Value < b.Value; });

    // Identities of entities the world no longer has stop being remembered.
    identity.ForgetDead(reading);
}

std::uint64_t ReplicationOwedFields(
    const ReplicatedComponent& component,
    const ReplicationChangeStore::ComponentState& held,
    std::uint64_t floor, bool ownershipMoved, bool forOwner)
{
    std::uint64_t owed = 0;
    for (std::size_t run = 0; run < component.Fields.size(); ++run)
    {
        const bool moved =
            run < held.ChangedAt.size() && held.ChangedAt[run] > floor;
        const bool gated =
            component.Fields[run].OwnerOnly || component.Fields[run].OwnerLocal;
        if (moved || (gated && ownershipMoved))
            owed |= (std::uint64_t{ 1 } << run);
    }
    return owed & ReplicationVisibleFields(component, forOwner);
}
