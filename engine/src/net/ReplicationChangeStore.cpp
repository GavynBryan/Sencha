#include <net/ReplicationChangeStore.h>

#include <ecs/Query.h>
#include <ecs/World.h>
#include <net/NetReplicationComponents.h>
#include <net/ReplicationCodec.h>
#include <world/RuntimeWorld.h>
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
    ClampedElements = 0;
}

namespace
{
    std::span<const std::byte> ElementAt(std::span<const std::byte> image, const ReplicationImageParts& parts,
                                         const ReplicatedList& list, std::size_t index, std::size_t element)
    {
        return image.subspan(parts.Lists[index].Offset + element * list.Stride, list.Stride);
    }

    void StampListsOnFirstSight(const ReplicatedComponent& layout, std::span<const std::byte> image, bool returning,
                                std::uint64_t generation, ReplicationChangeStore::ComponentState& added)
    {
        ReplicationImageParts parts;
        if (!ReplicationSplitImage(layout, image, parts))
            return;
        for (std::size_t index = 0; index < layout.Lists.size(); ++index)
        {
            const ReplicatedList& list = layout.Lists[index];
            const std::size_t count = parts.Lists[index].Count;
            added.CountChangedAt[index] = returning || count > 0 ? generation : 0;
            for (std::size_t element = 0; element < count; ++element)
            {
                const std::span<const std::byte> bytes = ElementAt(image, parts, list, index, element);
                for (const ReplicatedField& field : list.Fields)
                {
                    const bool atDefault = std::memcmp(bytes.data() + field.Offset,
                                                       list.ElementDefault.data() + field.Offset,
                                                       field.Size * field.Count)
                                        == 0;
                    added.ChangedAt.push_back(atDefault && !returning ? 0 : generation);
                }
            }
        }
    }

    void RestampLists(const ReplicatedComponent& layout, std::span<const std::byte> image, std::uint64_t generation,
                      std::vector<std::uint64_t>& stamps, ReplicationChangeStore::ComponentState& held)
    {
        ReplicationImageParts now;
        ReplicationImageParts before;
        const bool readable = ReplicationSplitImage(layout, held.Bytes, before);
        if (!ReplicationSplitImage(layout, image, now))
            return;
        const std::size_t headRuns = layout.Fields.size();
        stamps.assign(held.ChangedAt.begin(), held.ChangedAt.begin() + std::min(headRuns, held.ChangedAt.size()));
        stamps.resize(headRuns, generation);
        std::size_t cursor = headRuns;
        for (std::size_t index = 0; index < layout.Lists.size(); ++index)
        {
            const ReplicatedList& list = layout.Lists[index];
            const std::size_t runs = list.Fields.size();
            const std::size_t had = readable ? before.Lists[index].Count : 0;
            const std::size_t count = now.Lists[index].Count;
            for (std::size_t element = 0; element < count; ++element)
            {
                const bool kept = element < had && cursor + (element + 1) * runs <= held.ChangedAt.size();
                const std::uint64_t moved =
                    kept ? ReplicationChangedElementRuns(list, ElementAt(image, now, list, index, element),
                                                         ElementAt(held.Bytes, before, list, index, element))
                         : ~std::uint64_t{ 0 };
                for (std::size_t run = 0; run < runs; ++run)
                    stamps.push_back((moved & (std::uint64_t{ 1 } << run)) != 0
                                         ? generation
                                         : held.ChangedAt[cursor + element * runs + run]);
            }
            if (count != had || !readable)
                held.CountChangedAt[index] = generation;
            cursor += had * runs;
        }
        held.ChangedAt.swap(stamps);
    }

    std::uint32_t AnchoredPeer(const World& world, EntityId anchor, bool hasOwners, bool& pastBound)
    {
        pastBound = false;
        for (std::size_t depth = 0; depth < kNetRelevanceAnchorDepth; ++depth)
        {
            if (!world.IsAlive(anchor))
                return kNetAuthorityPeer;
            if (const NetRelevanceAnchor* next = world.TryGet<NetRelevanceAnchor>(anchor))
            {
                anchor = next->Anchor;
                continue;
            }
            const NetOwner* owner = hasOwners ? world.TryGet<NetOwner>(anchor) : nullptr;
            return owner != nullptr ? owner->Peer : kNetAuthorityPeer;
        }
        pastBound = true;
        return kNetAuthorityPeer;
    }
}

void ReplicationChangeStore::Update(World& world, const ReplicationLayout& layout,
                                    ReplicationAuthorityIdentity& identity,
                                    std::uint64_t generation,
                                    const RuntimeWorld* zones)
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

    std::vector<ReplicationImage> images;
    images.reserve(columns.size());
    for (const ResolvedComponent& column : columns)
        images.emplace_back(*column.Layout);

    std::vector<ComponentId> authorityOnly;
    for (const ResolvedComponent& column : columns)
        if (column.Layout->AuthorityMaterialized)
            authorityOnly.push_back(column.Column);

    const World& reading = world;
    const bool hasAnchors = world.IsRegistered(ResolveComponentTypeId<NetRelevanceAnchor>());
    const bool hasOwners = world.IsRegistered(ResolveComponentTypeId<NetOwner>());
    const bool hasPersistentIds =
        world.IsRegistered(ResolveComponentTypeId<PersistentIdComponent>());

    // Reused for every component of every entity. Most components have not
    // moved on most publishes, and that case must not pay an allocation just to
    // find out.
    std::vector<std::byte> scratch;
    std::vector<std::uint64_t> stamps;

    // A const walk: this reads the world without publishing a write, so
    // running it cannot make everything look changed on the next tick.
    Query<With<NetReplicated>> replicated(world);
    replicated.ForEachChunk([&](auto& view) {
        // Once per chunk. Zone residency is expressed as storage partitions and
        // a chunk lives in exactly one, so every row here shares an answer.
        ZoneId chunkZone;
        if (zones != nullptr && view.Partition() != PersistentStoragePartition)
        {
            if (const RuntimeZoneRecord* record =
                    zones->FindPartition(view.Partition()))
            {
                chunkZone = record->Id;
            }
        }

        for (std::uint32_t row = 0; row < view.Count(); ++row)
        {
            const EntityId entity = view.Entity(row);
            const NetEntityId id = identity.IdFor(entity);

            EntityState& state = Published[id];
            state.Id = id;
            state.SeenAt = generation;
            state.Zone = chunkZone;

            // Read every pass rather than once: an entity's authored identity
            // does not change, but which entity a NetEntityId names can, and
            // the store outlives neither.
            if (hasPersistentIds)
            {
                const PersistentIdComponent* authored = reading.TryGet<PersistentIdComponent>(entity);
                const bool materialized = std::ranges::any_of(
                    authorityOnly, [&](ComponentId column) { return reading.HasComponent(entity, column); });
                state.Persistent = authored != nullptr && !materialized ? authored->Id : PersistentEntityId{};
            }

            const NetRelevanceAnchor* anchor = hasAnchors ? reading.TryGet<NetRelevanceAnchor>(entity) : nullptr;
            state.Anchored = anchor != nullptr;
            state.RelevantPeer =
                anchor != nullptr ? AnchoredPeer(reading, anchor->Anchor, hasOwners, state.AnchorPastBound) : 0;
            state.AnchorPastBound = anchor != nullptr && state.AnchorPastBound;

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

            for (std::size_t c = 0; c < columns.size(); ++c)
            {
                const ResolvedComponent& column = columns[c];
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
                if (column.Layout->ToWire != nullptr)
                {
                    ReplicationImage& image = images[c];
                    image.Reset();
                    const ReplicationWireContext context{ .Entities = &world, .Entity = entity,
                                                          .Authority = &identity };
                    column.Layout->ToWire(context, std::span(rawBytes, column.Layout->LocalSize), image);
                    ClampedElements += image.Clamped();
                    scratch.assign(image.Bytes().begin(), image.Bytes().end());
                }
                else
                {
                    scratch.assign(rawBytes, rawBytes + size);
                }
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
                    const std::size_t removals = state.Removed.size();
                    std::erase_if(state.Removed,
                                  [&](const RemovedComponent& gone) {
                                      return gone.WireIndex == column.WireIndex;
                                  });
                    const bool returning = state.Removed.size() != removals;
                    ComponentState added;
                    added.WireIndex = column.WireIndex;
                    added.Bytes.assign(scratch.begin(), scratch.end());
                    added.ChangedAt.assign(runs, generation);
                    // A translated component's runs still at their default
                    // are what a receiver stages anyway. Not for one coming
                    // back: a peer never told it went still holds what it had.
                    const std::vector<std::byte>& defaults = column.Layout->WireDefault;
                    if (!returning && defaults.size() == size)
                    {
                        for (std::size_t run = 0; run < runs; ++run)
                        {
                            const ReplicatedField& field = column.Layout->Fields[run];
                            const std::size_t width = field.Size * field.Count;
                            if (std::memcmp(scratch.data() + field.Offset, defaults.data() + field.Offset, width) == 0)
                                added.ChangedAt[run] = 0;
                        }
                    }
                    if (!column.Layout->Lists.empty())
                        StampListsOnFirstSight(*column.Layout, scratch, returning, generation, added);
                    added.SeenAt = generation;
                    added.AddedAt = generation;
                    state.Components.push_back(std::move(added));
                    continue;
                }

                existing->SeenAt = generation;

                // A whole-component compare first, because most components do
                // not move on most publishes and this settles them in one pass
                // rather than one per run.
                if (existing->Bytes.size() == scratch.size()
                    && std::memcmp(existing->Bytes.data(), scratch.data(), scratch.size()) == 0)
                {
                    continue;
                }

                const std::uint64_t moved = ReplicationChangedFields(
                    *column.Layout, scratch, existing->Bytes);
                if (!column.Layout->Lists.empty())
                    RestampLists(*column.Layout, scratch, generation, stamps, *existing);
                existing->ChangedAt.resize(std::max(existing->ChangedAt.size(), runs), generation);
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
    std::uint64_t floor, bool ownershipMoved, bool forOwner,
    std::vector<std::uint64_t>* elements)
{
    // A run still at the default it was added with is what any peer holds
    // already, so an ownership move newly showing it to one says nothing.
    const auto owedRun = [&](const ReplicatedField& field, std::uint64_t changedAt) {
        const bool gated = field.OwnerOnly || field.OwnerLocal;
        return changedAt > floor || (gated && ownershipMoved && changedAt != 0);
    };

    std::uint64_t owed = 0;
    for (std::size_t run = 0; run < component.Fields.size(); ++run)
        if (run < held.ChangedAt.size() && owedRun(component.Fields[run], held.ChangedAt[run]))
            owed |= (std::uint64_t{ 1 } << run);
    owed &= ReplicationVisibleFields(component, forOwner);
    if (component.Lists.empty())
        return owed;

    ReplicationImageParts parts;
    if (!ReplicationSplitImage(component, held.Bytes, parts))
        return owed;
    std::size_t cursor = component.Fields.size();
    for (std::size_t index = 0; index < component.Lists.size(); ++index)
    {
        const ReplicatedList& list = component.Lists[index];
        const std::size_t runs = list.Fields.size();
        const std::size_t count = parts.Lists[index].Count;
        const std::size_t first = cursor;
        cursor += count * runs;
        const std::uint64_t visible = ReplicationVisibleElementRuns(list, forOwner);
        if (visible == 0 || first + count * runs > held.ChangedAt.size())
            continue;

        std::uint64_t gatedVisible = 0;
        for (std::size_t run = 0; run < runs; ++run)
            if (list.Fields[run].OwnerOnly || list.Fields[run].OwnerLocal)
                gatedVisible |= (std::uint64_t{ 1 } << run);
        gatedVisible &= visible;
        const std::uint64_t countAt = held.CountChangedAt[index];
        bool mentioned = countAt > floor || (gatedVisible != 0 && ownershipMoved && countAt != 0);

        const std::size_t written = elements != nullptr ? elements->size() : 0;
        for (std::size_t element = 0; element < count; ++element)
        {
            std::uint64_t carried = 0;
            for (std::size_t run = 0; run < runs; ++run)
                if (owedRun(list.Fields[run], held.ChangedAt[first + element * runs + run]))
                    carried |= (std::uint64_t{ 1 } << run);
            carried &= visible;
            mentioned = mentioned || carried != 0;
            if (elements != nullptr)
                elements->push_back(carried);
        }
        if (mentioned)
            owed |= std::uint64_t{ 1 } << (component.Fields.size() + index);
        else if (elements != nullptr)
            elements->resize(written);
    }
    return owed;
}
