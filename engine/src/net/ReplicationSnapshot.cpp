#include <net/ReplicationSnapshot.h>

#include <net/ReplicationChangeStore.h>

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
    constexpr std::uint8_t kSequenceBits = 32;
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

std::uint64_t ReplicationPeerState::Floor(NetEntityId id) const
{
    const auto it = Floors.find(id);
    return it == Floors.end() ? 0 : it->second;
}

bool ReplicationPeerState::Knows(NetEntityId id) const
{
    return Floors.contains(id);
}

void ReplicationPeerState::BeginSnapshot(std::uint32_t sequence)
{
    NextSequence = std::max(NextSequence, sequence + 1);

    if (Pending.size() < kMaxUnacknowledged)
    {
        if (Pending.empty() || Pending.back().Sequence != sequence)
            Pending.push_back(SentSnapshot{ .Sequence = sequence, .Entities = {}, .Destroyed = {} });
        return;
    }

    // This peer has not confirmed anything for longer than is worth tracking.
    // Everything believed about it is a guess by now, so it is treated as new:
    // the snapshot about to be written is full state, and it costs one snapshot
    // rather than an unbounded pile of them.
    Floors.clear();
    Pending.clear();
    Pending.push_back(SentSnapshot{ .Sequence = sequence, .Entities = {}, .Destroyed = {} });
}

void ReplicationPeerState::RecordSent(std::uint32_t sequence, NetEntityId id,
                                      std::uint64_t generation)
{
    // Held aside, not applied. How far a peer has been carried only changes
    // when the peer says so, because a snapshot that was written is not a
    // snapshot that arrived.
    if (Pending.empty() || Pending.back().Sequence != sequence)
        BeginSnapshot(sequence);

    Pending.back().Entities.emplace_back(id, generation);
}

void ReplicationPeerState::NoteDeparted(NetEntityId id)
{
    // Only a peer that was told about it can be owed the news.
    if (Floors.erase(id) == 0 && std::find(Departed.begin(), Departed.end(), id)
                                     == Departed.end())
    {
        return;
    }
    if (std::find(Departed.begin(), Departed.end(), id) == Departed.end())
        Departed.push_back(id);
}

void ReplicationPeerState::RecordDestroysSent(std::uint32_t sequence,
                                              std::span<const NetEntityId> ids)
{
    if (ids.empty())
        return;
    if (Pending.empty() || Pending.back().Sequence != sequence)
        BeginSnapshot(sequence);
    Pending.back().Destroyed.assign(ids.begin(), ids.end());
}

void ReplicationPeerState::Acknowledge(const NetSnapshotAck& ack)
{
    if (!ack.Any())
        return;

    std::size_t settled = 0;
    for (const SentSnapshot& snapshot : Pending)
    {
        if (ack.Confirms(snapshot.Sequence))
        {
            for (const auto& [id, generation] : snapshot.Entities)
            {
                std::uint64_t& floor = Floors[id];
                floor = std::max(floor, generation);
            }
            // Heard and understood: the debt is settled.
            for (NetEntityId id : snapshot.Destroyed)
                std::erase(Departed, id);
        }
        else if (snapshot.Sequence > ack.Newest())
        {
            // Still in flight. Everything behind it is too, because sequences
            // leave in order.
            break;
        }
        // Otherwise: sent, never confirmed, and no proof can still arrive. It
        // is dropped without raising anything -- what it carried stays owed,
        // and the next snapshot describes it again.
        ++settled;
    }

    Pending.erase(Pending.begin(),
                  Pending.begin() + static_cast<std::ptrdiff_t>(settled));
}

void ReplicationPeerState::Forget(NetEntityId id)
{
    Floors.erase(id);
    std::erase(Departed, id);
    for (SentSnapshot& snapshot : Pending)
    {
        std::erase_if(snapshot.Entities, [id](const auto& entry) {
            return entry.first == id;
        });
        std::erase(snapshot.Destroyed, id);
    }
}

void ReplicationPeerState::Clear()
{
    Floors.clear();
    Departed.clear();
    Pending.clear();
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

namespace
{
    // Which runs of a component a peer is owed: the ones that moved after its
    // floor, plus every gated one when ownership moved after its floor, and
    // then only those this peer may see at all.
    std::uint64_t OwedFields(const ReplicatedComponent& component,
                             const ReplicationChangeStore::ComponentState& held,
                             std::uint64_t floor, bool ownershipMoved,
                             bool forOwner)
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

    // Whether a snapshot for this peer would carry anything about this entity.
    // Asked before the envelope is written, because an entity with nothing to
    // say should not appear at all rather than appear with every mask clear.
    bool EntityHasNewsFor(const ReplicationChangeStore::EntityState& entity,
                          const ReplicationLayout& layout,
                          const ReplicationPeerState& peer,
                          std::uint32_t ownerPeer)
    {
        // Never told about it: the spawn is the news.
        if (!peer.Knows(entity.Id))
            return true;

        const std::uint64_t floor = peer.Floor(entity.Id);
        const bool isOwner = ownerPeer != 0 && entity.Owner == ownerPeer;
        const bool ownershipMoved = entity.OwnerChangedAt > floor;

        for (const ReplicationChangeStore::ComponentState& held : entity.Components)
        {
            const ReplicatedComponent* component = layout.At(held.WireIndex);
            if (component == nullptr)
                continue;
            if (OwedFields(*component, held, floor, ownershipMoved, isOwner) != 0)
                return true;
        }
        return false;
    }
}

SnapshotWriteResult ReplicationWriteSnapshot(const SnapshotWriteRequest& request,
                                             std::span<std::byte> out)
{
    SnapshotWriteResult result;
    if (request.Layout == nullptr || request.Peer == nullptr
        || request.Changes == nullptr)
    {
        return result;
    }

    // Zero is the acknowledgement's "nothing yet", so a snapshot written under
    // it can never be confirmed: every delta would be measured from first
    // contact forever, which reads as bandwidth rather than as a defect.
    assert(request.Sequence != 0
           && "A snapshot needs a sequence; take it from the peer state.");

    const ReplicationLayout& layout = *request.Layout;
    const ReplicationChangeStore& changes = *request.Changes;
    ReplicationPeerState& peer = *request.Peer;
    const std::uint64_t generation = changes.Generation();

    // Before any difference is computed, so the whole snapshot is measured from
    // one set of floors rather than from ones that moved part way through.
    peer.BeginSnapshot(request.Sequence);

    // Anything this peer was told about and the world no longer has. Kept per
    // peer rather than read straight off the store, because a peer that was
    // never told about an entity has nothing to forget, and one whose destroy
    // was lost still needs telling.
    for (NetEntityId id : changes.Departed())
        peer.NoteDeparted(id);
    const std::span<const NetEntityId> destroyed = peer.OwedDestroys();

    // Which entities this snapshot has anything to say about. An entity nobody
    // has touched since this peer's floor has nothing to carry, and absence
    // means "unchanged" on the far side -- so saying nothing is the whole
    // message. It used to cost around ten bytes of envelope to say nothing,
    // paid per entity per peer per snapshot, which is what a still world spent
    // its bandwidth on.
    std::vector<const ReplicationChangeStore::EntityState*> sending;
    sending.reserve(changes.Size());
    for (const ReplicationChangeStore::EntityState& entity : changes.Live())
    {
        if (EntityHasNewsFor(entity, layout, peer, request.OwnerPeer))
            sending.push_back(&entity);
    }

    const std::size_t entityCap = ReplicationDefaultCaps().MaxEntitiesPerSnapshot;
    const std::size_t entityCount = std::min(sending.size(), entityCap);

    NetBitWriter writer(out);
    writer.WriteU64(request.Tick);
    writer.WriteBits(request.Sequence, kSequenceBits);
    writer.WriteU64(request.CommandAck);
    writer.WriteBits(static_cast<std::uint32_t>(destroyed.size()), kCountBits);
    writer.WriteBits(static_cast<std::uint32_t>(entityCount), kCountBits);

    for (NetEntityId id : destroyed)
        WriteNetEntityId(writer, id);

    // What each entity was carried to, recorded only once the whole snapshot
    // has encoded: a partial record would raise a floor for state the peer
    // never received.
    std::vector<std::pair<NetEntityId, std::uint64_t>> carried;
    carried.reserve(entityCount);

    for (std::size_t index = 0; index < entityCount; ++index)
    {
        const ReplicationChangeStore::EntityState& entity = *sending[index];
        const std::uint64_t floor = peer.Floor(entity.Id);
        const bool isOwner =
            request.OwnerPeer != 0 && entity.Owner == request.OwnerPeer;
        // A transfer makes owner-gated runs newly visible to one peer and newly
        // hidden from another without any of them having changed value, so the
        // fields' own history cannot express it.
        const bool ownershipMoved = entity.OwnerChangedAt > floor;

        WriteNetEntityId(writer, entity.Id);
        writer.WriteBits(static_cast<std::uint32_t>(entity.Components.size()),
                         kComponentCountBits);

        for (const ReplicationChangeStore::ComponentState& held : entity.Components)
        {
            const ReplicatedComponent* component = layout.At(held.WireIndex);
            writer.WriteBits(held.WireIndex, kComponentIndexBits);

            const std::uint64_t fields =
                OwedFields(*component, held, floor, ownershipMoved, isOwner);

            if (!ReplicationEncodeComponent(*component, held.Bytes, fields, writer))
                return result;  // Did not fit; the peer state is left untouched.
        }

        carried.emplace_back(entity.Id, generation);
    }

    if (writer.Overflowed())
        return result;

    peer.RecordDestroysSent(request.Sequence, destroyed);
    for (const auto& [id, at] : carried)
        peer.RecordSent(request.Sequence, id, at);

    result.Ok = true;
    result.BytesWritten = writer.BytesWritten();
    result.EntitiesWritten = static_cast<std::uint32_t>(entityCount);
    result.EntitiesDestroyed = static_cast<std::uint32_t>(destroyed.size());
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
        || !reader.ReadBits(kSequenceBits, result.Sequence)
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

    // Every snapshot a predicting client applies is a chance to reconcile,
    // whether or not it mentioned the pawn. The pawn is absent exactly when the
    // authority has not moved it -- which is the case a client predicting
    // movement of its own is most likely to be wrong about, and the acknowledged
    // command tick has moved on regardless.
    if (request.Prediction != nullptr && request.Prediction->Predicted().IsValid())
        result.ReconcilePredicted = true;

    return result;
}
