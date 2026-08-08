#include <net/ReplicationSnapshot.h>

#include <ecs/Query.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <net/NetReplicationComponents.h>
#include <net/ReplicationInterpolation.h>
#include <world/transform/DerivedTransform.h>

#include <algorithm>
#include <cassert>
#include <cstring>

namespace
{
    // Counts on the wire are 32 bits so the format does not need revisiting for
    // a bigger world; the caps, not the width, are what bound the work.
    constexpr std::uint8_t kCountBits = 32;
    constexpr std::uint8_t kComponentCountBits = 8;
    constexpr std::uint8_t kComponentIndexBits = 8;

    void WriteNetEntityId(NetBitWriter& writer, NetEntityId id)
    {
        writer.WriteU64(id.Value);
    }

    bool ReadNetEntityId(NetBitReader& reader, NetEntityId& out)
    {
        std::uint64_t value = 0;
        if (!reader.ReadU64(value))
            return false;
        out = NetEntityId{ value };
        return true;
    }
}

const ReplicationCaps& ReplicationDefaultCaps()
{
    static const ReplicationCaps caps;
    return caps;
}

std::string_view SnapshotApplyErrorToString(SnapshotApplyError error)
{
    switch (error)
    {
    case SnapshotApplyError::None:                   return "none";
    case SnapshotApplyError::Truncated:              return "truncated";
    case SnapshotApplyError::CapExceeded:            return "cap exceeded";
    case SnapshotApplyError::UnknownComponent:       return "unknown component key";
    case SnapshotApplyError::UnknownComponentStorage:
        return "no storage for component";
    case SnapshotApplyError::ComponentAddFailed:     return "could not add component";
    }
    return "unknown";
}

//=============================================================================
// ReplicationPeerState
//=============================================================================

const ReplicationPeerState::EntityBaseline* ReplicationPeerState::Find(
    NetEntityId id) const
{
    const auto it = Baselines.find(id);
    return it == Baselines.end() ? nullptr : &it->second;
}

void ReplicationPeerState::Record(NetEntityId id, std::uint8_t component,
                                  std::span<const std::byte> bytes)
{
    std::vector<std::byte>& stored = Baselines[id].Components[component];
    stored.assign(bytes.begin(), bytes.end());
}

void ReplicationPeerState::Forget(NetEntityId id)
{
    Baselines.erase(id);
}

//=============================================================================
// Identity
//=============================================================================

NetEntityId ReplicationAuthorityIdentity::IdFor(EntityId entity)
{
    const auto it = Forward.find(entity);
    if (it != Forward.end())
        return it->second;

    const NetEntityId minted{ NextId++ };
    Forward.emplace(entity, minted);
    return minted;
}

NetEntityId ReplicationAuthorityIdentity::TryFind(EntityId entity) const
{
    const auto it = Forward.find(entity);
    return it == Forward.end() ? NetEntityId{} : it->second;
}

void ReplicationAuthorityIdentity::Release(EntityId entity)
{
    Forward.erase(entity);
}

void ReplicationAuthorityIdentity::ForgetDead(const World& world)
{
    std::erase_if(Forward, [&world](const auto& entry) {
        return !world.IsAlive(entry.first);
    });
}

EntityId ReplicationClientIdentity::TryResolve(NetEntityId id) const
{
    const auto it = Entries.find(id);
    return it == Entries.end() ? EntityId{} : it->second;
}

void ReplicationClientIdentity::Bind(NetEntityId id, EntityId entity)
{
    Entries[id] = entity;
}

void ReplicationClientIdentity::Unbind(NetEntityId id)
{
    Entries.erase(id);
}

//=============================================================================
// Writing
//=============================================================================

SnapshotWriteResult ReplicationWriteSnapshot(const SnapshotWriteRequest& request,
                                             std::span<std::byte> out)
{
    SnapshotWriteResult result;
    if (request.Source == nullptr || request.Layout == nullptr
        || request.Identity == nullptr || request.Peer == nullptr)
    {
        return result;
    }

    World& world = *request.Source;
    const World& reading = world;
    const ReplicationLayout& layout = *request.Layout;
    ReplicationPeerState& peer = *request.Peer;
    const bool hasOwners = world.IsRegistered(ResolveComponentTypeId<NetOwner>());

    // Nothing is marked for replication in a world that never registered the
    // marker, which is every single-player world.
    if (!world.IsRegistered(ResolveComponentTypeId<NetReplicated>()))
        return result;

    // Which entity carries which component, resolved once rather than per
    // entity: a ComponentTypeId lookup is a hash probe and this loop is the
    // hot one.
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
            continue;  // The authority does not store it; nothing to send.
        columns.push_back(ResolvedComponent{
            .WireIndex = static_cast<std::uint8_t>(i),
            .Column = world.GetComponentIdByType(component->Type),
            .Layout = component,
        });
    }

    // Pass one: what exists now, and what it looks like at wire precision.
    struct PendingEntity
    {
        NetEntityId Id;
        // Zero when nobody owns it, which is the case for everything the
        // authority drives itself.
        std::uint32_t Owner = 0;
        // Wire index and the snapped bytes to send.
        std::vector<std::pair<std::uint8_t, std::vector<std::byte>>> Components;
    };
    std::vector<PendingEntity> live;

    // A const query: this walks the world without publishing a write, so
    // running the writer cannot make everything look changed next tick.
    Query<With<NetReplicated>> replicated(world);
    replicated.ForEachChunk([&](auto& view) {
        for (std::uint32_t row = 0; row < view.Count(); ++row)
        {
            const EntityId entity = view.Entity(row);
            PendingEntity pending;
            pending.Id = request.Identity->IdFor(entity);
            if (hasOwners)
            {
                if (const NetOwner* owner = reading.TryGet<NetOwner>(entity))
                    pending.Owner = owner->Peer;
            }

            for (const ResolvedComponent& column : columns)
            {
                if (!reading.HasComponent(entity, column.Column))
                    continue;
                const void* raw = reading.GetComponentRaw(entity, column.Column);
                if (raw == nullptr)
                    continue;

                std::vector<std::byte> bytes(column.Layout->Size);
                std::memcpy(bytes.data(), raw, column.Layout->Size);
                // Snapped before it is compared or sent, so the baseline this
                // records is exactly what the peer will hold.
                ReplicationSnapToWire(*column.Layout, bytes);
                pending.Components.emplace_back(column.WireIndex, std::move(bytes));
            }

            if (!pending.Components.empty())
                live.push_back(std::move(pending));
        }
    });

    if (live.size() > ReplicationDefaultCaps().MaxEntitiesPerSnapshot)
        live.resize(ReplicationDefaultCaps().MaxEntitiesPerSnapshot);

    // Pass two: anything the peer was told about and is not here any more.
    std::vector<NetEntityId> destroyed;
    for (const auto& [id, baseline] : peer.All())
    {
        const bool stillLive = std::any_of(
            live.begin(), live.end(),
            [id = id](const PendingEntity& entity) { return entity.Id == id; });
        if (!stillLive)
            destroyed.push_back(id);
    }
    // Deterministic order: an unordered_map's iteration order is not a contract,
    // and two runs of the same simulation must produce the same bytes.
    std::sort(destroyed.begin(), destroyed.end(),
              [](NetEntityId a, NetEntityId b) { return a.Value < b.Value; });
    std::sort(live.begin(), live.end(),
              [](const PendingEntity& a, const PendingEntity& b) {
                  return a.Id.Value < b.Id.Value;
              });

    NetBitWriter writer(out);
    writer.WriteU64(request.Tick);
    writer.WriteU64(request.CommandAck);
    writer.WriteBits(static_cast<std::uint32_t>(destroyed.size()), kCountBits);
    writer.WriteBits(static_cast<std::uint32_t>(live.size()), kCountBits);

    for (NetEntityId id : destroyed)
        WriteNetEntityId(writer, id);

    for (const PendingEntity& entity : live)
    {
        WriteNetEntityId(writer, entity.Id);
        writer.WriteBits(static_cast<std::uint32_t>(entity.Components.size()),
                         kComponentCountBits);

        for (const auto& [wireIndex, bytes] : entity.Components)
        {
            const ReplicatedComponent* component = layout.At(wireIndex);
            writer.WriteBits(wireIndex, kComponentIndexBits);

            // An entity or component this peer has not been told about gets
            // full state; there is nothing to difference against.
            std::span<const std::byte> baseline;
            if (const ReplicationPeerState::EntityBaseline* known =
                    peer.Find(entity.Id))
            {
                const auto it = known->Components.find(wireIndex);
                if (it != known->Components.end())
                    baseline = it->second;
            }

            const bool isOwner =
                request.OwnerPeer != 0 && entity.Owner == request.OwnerPeer;
            if (!ReplicationEncodeComponent(*component, bytes, baseline, isOwner,
                                            writer))
            {
                return result;  // Did not fit; the peer state is left untouched.
            }
        }
    }

    if (writer.Overflowed())
        return result;

    // Committed only once the whole snapshot encoded: a partial record would
    // make the next delta reference bytes the peer never received.
    for (NetEntityId id : destroyed)
        peer.Forget(id);
    for (const PendingEntity& entity : live)
    {
        for (const auto& [wireIndex, bytes] : entity.Components)
            peer.Record(entity.Id, wireIndex, bytes);
    }

    // Identities of entities the world no longer has stop being remembered.
    // Keyed on liveness rather than on what this peer was told, because the map
    // is shared by every peer while the baselines are per peer.
    request.Identity->ForgetDead(reading);

    result.Ok = true;
    result.EntitiesWritten = static_cast<std::uint32_t>(live.size());
    result.EntitiesDestroyed = static_cast<std::uint32_t>(destroyed.size());
    result.BytesWritten = writer.BytesWritten();
    return result;
}

//=============================================================================
// Applying
//=============================================================================

SnapshotApplyResult ReplicationApplySnapshot(const SnapshotApplyRequest& request,
                                             std::span<const std::byte> bytes)
{
    SnapshotApplyResult result;
    if (request.Target == nullptr || request.Schema == nullptr
        || request.Layout == nullptr || request.Identity == nullptr)
    {
        result.Error = SnapshotApplyError::Truncated;
        return result;
    }

    World& world = *request.Target;
    const WorldComponentSchema& schema = *request.Schema;
    const ReplicationLayout& layout = *request.Layout;
    ReplicationClientIdentity& identity = *request.Identity;
    const ReplicationCaps& caps = ReplicationDefaultCaps();

    NetBitReader reader(bytes);

    std::uint32_t destroyedCount = 0;
    std::uint32_t updatedCount = 0;
    if (!reader.ReadU64(result.Tick)
        || !reader.ReadU64(result.CommandAck)
        || !reader.ReadBits(kCountBits, destroyedCount)
        || !reader.ReadBits(kCountBits, updatedCount))
    {
        result.Error = SnapshotApplyError::Truncated;
        return result;
    }

    // Checked before either count is used to loop, so a peer cannot make this
    // spin by claiming four billion entities.
    if (destroyedCount > caps.MaxEntitiesPerSnapshot
        || updatedCount > caps.MaxEntitiesPerSnapshot)
    {
        result.Error = SnapshotApplyError::CapExceeded;
        return result;
    }

    for (std::uint32_t i = 0; i < destroyedCount; ++i)
    {
        NetEntityId id;
        if (!ReadNetEntityId(reader, id))
        {
            result.Error = SnapshotApplyError::Truncated;
            return result;
        }

        const EntityId entity = identity.TryResolve(id);
        identity.Unbind(id);
        // Poses held for an entity that is gone describe nothing, and the handle
        // will be handed out again to something else.
        if (request.Interpolation != nullptr && entity.IsValid())
            request.Interpolation->Forget(entity);
        // An identity this client never had is not an error: it can be an
        // entity destroyed before the client was ever told it existed.
        if (entity.IsValid() && world.IsAlive(entity))
        {
            world.DestroyEntity(entity);
            ++result.EntitiesDestroyed;
        }
    }

    std::vector<std::byte> staging;
    for (std::uint32_t i = 0; i < updatedCount; ++i)
    {
        NetEntityId id;
        std::uint32_t componentCount = 0;
        if (!ReadNetEntityId(reader, id)
            || !reader.ReadBits(kComponentCountBits, componentCount))
        {
            result.Error = SnapshotApplyError::Truncated;
            return result;
        }
        if (componentCount > caps.MaxComponentsPerEntity)
        {
            result.Error = SnapshotApplyError::CapExceeded;
            return result;
        }

        EntityId entity = identity.TryResolve(id);
        const bool spawned = !entity.IsValid() || !world.IsAlive(entity);
        if (spawned)
        {
            entity = world.CreateEntity(StoragePartitionId{ request.Partition });
            identity.Bind(id, entity);
            ++result.EntitiesSpawned;
        }
        else
        {
            ++result.EntitiesUpdated;
        }

        for (std::uint32_t c = 0; c < componentCount; ++c)
        {
            std::uint32_t wireIndex = 0;
            if (!reader.ReadBits(kComponentIndexBits, wireIndex))
            {
                result.Error = SnapshotApplyError::Truncated;
                return result;
            }

            const ReplicatedComponent* component =
                layout.At(static_cast<std::uint8_t>(wireIndex));
            if (component == nullptr)
            {
                result.Error = SnapshotApplyError::UnknownComponent;
                return result;
            }
            if (!world.IsRegistered(component->Type))
            {
                result.Error = SnapshotApplyError::UnknownComponentStorage;
                return result;
            }

            // Decoded into staging first. A delta leaves unmasked fields
            // alone, so staging has to start as whatever those fields should
            // keep: what the entity already holds, or -- on an entity meeting
            // this component for the first time -- the type's own defaults.
            //
            // Not zeroes. A field the wire never carries is a field the sender
            // is saying nothing about, either because it is local to each
            // machine or because it belongs to the owner; zeroing it substitutes
            // a value the type never declared. A pitch limit of zero is a
            // player who cannot look up, on a component that decoded perfectly.
            // A predicted entity's position never reaches the world through
            // here. It is decoded onto the authority's own view of it, which is
            // what the delta is against, and handed to the predictor to argue
            // with what this machine simulated.
            if (request.Prediction != nullptr
                && request.Prediction->Intercepts(entity, component->Type))
            {
                const std::span<std::byte> shadow =
                    request.Prediction->AuthoritativeBytes(component->Type);
                if (shadow.size() != component->Size)
                {
                    result.Error = SnapshotApplyError::UnknownComponentStorage;
                    return result;
                }
                // Seeding the shadow the first time. A client adopts its pawn
                // only after snapshots have already been arriving for it, so
                // the authority's baseline already credits this machine with
                // values it has no reason to send again. The world's copy is
                // exactly those values, which makes it the only correct seed --
                // starting from the type's defaults would silently discard
                // everything said before the pawn became this machine's own.
                if (!request.Prediction->HasAuthoritativeState(component->Type))
                {
                    const ComponentId column =
                        world.GetComponentIdByType(component->Type);
                    const void* held = world.HasComponent(entity, column)
                                           ? world.GetComponentRaw(entity, column)
                                           : nullptr;
                    if (held != nullptr)
                    {
                        std::memcpy(shadow.data(), held, component->Size);
                    }
                    else if (!schema.WriteDefaultBytes(component->Type, shadow))
                    {
                        result.Error = SnapshotApplyError::UnknownComponentStorage;
                        return result;
                    }
                }
                if (!ReplicationDecodeComponent(*component, reader, shadow))
                {
                    result.Error = SnapshotApplyError::Truncated;
                    return result;
                }
                request.Prediction->MarkSeen(component->Type);
                // Last one wins within a snapshot; a correction describes the
                // tick, not the component.
                if (const auto correction = request.Prediction->Commit(result.Tick))
                    result.Prediction = correction;
                continue;
            }

            // Everything this machine mirrors rather than simulates. The pose is
            // held with the tick it describes instead of written, because the
            // tick it describes is behind the one about to be drawn, and writing
            // it now is what makes a mirrored entity step whenever its datagram
            // was late.
            // Never the pawn this machine simulates for itself, whether or not
            // it is currently correcting it: with prediction off the local pawn
            // still runs its own movement here, and holding its pose back would
            // leave the authority's word with nowhere to land.
            const bool ownPawn = request.Prediction != nullptr
                              && request.Prediction->Predicts(entity);
            if (request.Interpolation != nullptr && !ownPawn
                && request.Interpolation->Intercepts(component->Type))
            {
                const std::span<std::byte> shadow =
                    request.Interpolation->AuthoritativeBytes(entity);
                if (shadow.size() != component->Size)
                {
                    result.Error = SnapshotApplyError::UnknownComponentStorage;
                    return result;
                }
                if (!request.Interpolation->HasAuthoritativeState(entity)
                    && !schema.WriteDefaultBytes(component->Type, shadow))
                {
                    result.Error = SnapshotApplyError::UnknownComponentStorage;
                    return result;
                }
                if (!ReplicationDecodeComponent(*component, reader, shadow))
                {
                    result.Error = SnapshotApplyError::Truncated;
                    return result;
                }
                request.Interpolation->Commit(entity, result.Tick);

                // The component still has to exist, because presenting a pose
                // means writing into it every tick and nothing can be written
                // into a column the entity never gained. Seeded from the
                // authority's own value the first time, so a newly mirrored
                // entity appears where it belongs rather than at the origin and
                // then slides in from there.
                if (!world.HasComponent(entity,
                                        world.GetComponentIdByType(component->Type))
                    && !schema.ImportComponent(world, entity, component->Type, shadow))
                {
                    result.Error = SnapshotApplyError::ComponentAddFailed;
                    return result;
                }
                continue;
            }

            staging.assign(component->Size, std::byte{ 0 });
            const ComponentId column = world.GetComponentIdByType(component->Type);
            const bool present = world.HasComponent(entity, column);
            if (present)
            {
                const void* current = world.GetComponentRaw(entity, column);
                if (current != nullptr)
                    std::memcpy(staging.data(), current, component->Size);
            }
            else if (!schema.WriteDefaultBytes(component->Type, staging))
            {
                result.Error = SnapshotApplyError::UnknownComponentStorage;
                return result;
            }

            if (!ReplicationDecodeComponent(*component, reader, staging))
            {
                result.Error = SnapshotApplyError::Truncated;
                return result;
            }

            const bool wrote =
                present
                    ? schema.SetComponentBytes(world, entity, component->Type, staging)
                    : schema.ImportComponent(world, entity, component->Type, staging);
            if (!wrote)
            {
                result.Error = SnapshotApplyError::ComponentAddFailed;
                return result;
            }
        }

        // Derived from the local transform that just arrived, and re-seeded on
        // every update so the pair stays consistent between the write and the
        // propagation that follows it. Without this an entity is correct in
        // state and invisible on screen: extraction and pose history both read
        // the world transform, and nothing else would ever create it here.
        SeedDerivedWorldTransform(world, entity);

        // Last, and only once: the recipe completes an entity that already
        // holds everything the wire had to say about it.
        if (spawned && request.Recipes != nullptr)
        {
            NetSpawnRecipeId recipeId = kNetNoSpawnRecipe;
            if (world.IsRegistered<NetSpawnRecipe>())
            {
                if (const NetSpawnRecipe* recipe = world.TryGet<NetSpawnRecipe>(entity))
                    recipeId = recipe->Id;
            }
            if (recipeId != kNetNoSpawnRecipe
                && !request.Recipes->Build(recipeId, world, entity))
            {
                ++result.RecipesMissing;
            }
        }
    }

    return result;
}
