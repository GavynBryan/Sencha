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
    // A count is bounded by the entity cap and checked against it on the way in,
    // so a width that could not address the cap would be a decoder refusing
    // snapshots the writer is allowed to produce.
    static_assert(ReplicationCaps{}.MaxEntitiesPerSnapshot
                      <= (1u << ReplicationSnapshotWire::CountBits),
                  "the count width has to address the cap it is checked against");
    constexpr std::uint8_t kCountBits = ReplicationSnapshotWire::CountBits;
    constexpr std::uint8_t kSequenceBits = ReplicationSnapshotWire::SequenceBits;
    constexpr std::uint8_t kComponentCountBits =
        ReplicationSnapshotWire::ComponentCountBits;
    constexpr std::uint8_t kComponentIndexBits =
        ReplicationSnapshotWire::ComponentIndexBits;

    void WriteNetEntityId(NetBitWriter& writer, NetEntityId id)
    {
        writer.WriteVarUInt(id.Value);
    }

    bool ReadNetEntityId(NetBitReader& reader, NetEntityId& out)
    {
        std::uint64_t value = 0;
        if (!reader.ReadVarUInt(value))
            return false;
        out = NetEntityId{ value };
        return true;
    }

    // What one identity costs on the wire, which the fill has to know before it
    // writes one. Not a constant any more: an identity is as wide as it needs
    // to be.
    std::size_t NetEntityIdBits(NetEntityId id)
    {
        std::size_t groups = 1;
        for (std::uint64_t rest = id.Value >> 7; rest != 0; rest >>= 7)
            ++groups;
        return groups * 8;
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
    const auto it = Entities.find(id);
    return it == Entities.end() ? 0 : it->second.Floor;
}

bool ReplicationPeerState::Knows(NetEntityId id) const
{
    // Proof, not a record of having tried. A generation is one-based, so a
    // non-zero floor is exactly "this peer has confirmed something about it".
    return Floor(id) != 0;
}

std::uint32_t ReplicationPeerState::LastSentAt(NetEntityId id) const
{
    const auto it = Entities.find(id);
    return it == Entities.end() ? 0 : it->second.LastSentAt;
}

void ReplicationPeerState::BeginSnapshot(std::uint32_t sequence)
{
    NextSequence = std::max(NextSequence, sequence + 1);
    if (First == 0)
        First = sequence;

    // The oldest one goes rather than the floors it was waiting to raise. It is
    // dropped exactly as an expired one is: what it carried simply stays owed,
    // and the next snapshot that has room describes it again. Clearing the
    // floors instead would throw away every entity this peer has genuinely
    // confirmed, which under a byte budget is worse than the silence it is
    // reacting to -- the seeding it forces competes for room with the state
    // that actually moved.
    if (Pending.size() >= kMaxUnacknowledged)
        Pending.erase(Pending.begin());

    if (Pending.empty() || Pending.back().Sequence != sequence)
    {
        Pending.push_back(
            SentSnapshot{ .Sequence = sequence, .Entities = {}, .Destroyed = {} });
    }
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
    // Whose turn it was, which is settled by the writing and not by the
    // arriving: an entity resent every snapshot to a peer whose acknowledgements
    // are lost is being served, not starved.
    EntityRecord& record = Entities[id];
    record.LastSentAt = std::max(record.LastSentAt, sequence);
}

void ReplicationPeerState::NoteDeparted(NetEntityId id)
{
    // Only a peer that has been told about it can be owed the news -- but told
    // means written to, not confirmed. A spawn whose acknowledgement was lost
    // may still have arrived, so the client may be holding the entity; a destroy
    // for one it never had is discarded on arrival, and a destroy withheld from
    // one that did leaves it there forever.
    const bool tracked = Entities.erase(id) != 0;
    if (!tracked)
        return;
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
                EntityRecord& record = Entities[id];
                record.Floor = std::max(record.Floor, generation);
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
    Entities.erase(id);
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
    Entities.clear();
    Departed.clear();
    Pending.clear();
    First = 0;
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

    // The fixed part of every snapshot: the tick, this snapshot's name, the
    // command acknowledgement, and the two counts.
    constexpr std::size_t kHeaderBits = ReplicationSnapshotWire::TickBits
                                      + kSequenceBits
                                      + ReplicationSnapshotWire::CommandAckBits
                                      + kCountBits + kCountBits;

    // What a snapshot would say about one entity for one peer, and what saying
    // it would cost. Decided before anything is written, because a bit writer
    // cannot be rewound: an entity discovered not to fit half way through its
    // own encoding cannot be taken back out.
    struct PlannedEntity
    {
        const ReplicationChangeStore::EntityState* Entity = nullptr;
        // Which snapshot last carried it to this peer. Zero has never been sent,
        // and sorts first for that reason.
        std::uint32_t LastSentAt = 0;
        std::size_t Bits = 0;
        // The owed field masks, one per component, in the entity's component
        // order. Held rather than recomputed at write time so the bytes written
        // are necessarily the bytes measured.
        std::size_t FirstMask = 0;
        // Driven by the peer this snapshot is for. Everything else this peer
        // receives is mirrored and presented a fixed distance in the past, so a
        // late sample is smoothed over; its own entity is what its prediction
        // argues with, and a late one means it keeps predicting uncorrected
        // until the correction is large enough to see as a jump.
        bool Owned = false;
    };

    // Plans one entity, appending its masks. Returns false when this peer is
    // owed nothing about it: absence means "unchanged" on the far side, so
    // saying nothing is the whole message, and an entity that appeared with
    // every mask clear would cost its envelope to say it.
    bool PlanEntity(const ReplicationChangeStore::EntityState& entity,
                    const ReplicationLayout& layout,
                    const ReplicationPeerState& peer, std::uint32_t ownerPeer,
                    std::vector<std::uint64_t>& masks, PlannedEntity& out)
    {
        const std::uint64_t floor = peer.Floor(entity.Id);
        const bool isOwner = ownerPeer != 0 && entity.Owner == ownerPeer;
        // A transfer makes owner-gated runs newly visible to one peer and newly
        // hidden from another without any of them having changed value, so the
        // fields' own history cannot express it.
        const bool ownershipMoved = entity.OwnerChangedAt > floor;

        const std::size_t first = masks.size();
        std::size_t bits = NetEntityIdBits(entity.Id) + kComponentCountBits;
        bool owedAnything = false;

        for (const ReplicationChangeStore::ComponentState& held : entity.Components)
        {
            const ReplicatedComponent* component = layout.At(held.WireIndex);
            // The store only ever records components the layout has, so a
            // missing one means the two disagree about the table they were built
            // from, and the index about to be written names nothing.
            assert(component != nullptr && "store holds a component the layout lost");

            const std::uint64_t owed =
                OwedFields(*component, held, floor, ownershipMoved, isOwner);
            masks.push_back(owed);
            owedAnything = owedAnything || owed != 0;
            bits += kComponentIndexBits
                  + ReplicationEncodedComponentBits(*component, owed);
        }

        // An entity this peer has confirmed nothing about still needs announcing
        // even when every field sits where it was: the announcement is what makes
        // the client create it at all, and an entity whose replicated state is
        // all defaults is exactly the case where no field has moved.
        if (!owedAnything && peer.Knows(entity.Id))
        {
            masks.resize(first);
            return false;
        }

        out = PlannedEntity{
            .Entity = &entity,
            .LastSentAt = peer.LastSentAt(entity.Id),
            .Bits = bits,
            .FirstMask = first,
            .Owned = isOwner,
        };
        return true;
    }

    // Records a snapshot always has room for, however much the peer's own
    // entities want. Destroys are the one record nothing later repeats, so a
    // budget that could be taken entirely by updates would leave a client
    // holding an entity that is never mentioned again.
    constexpr std::size_t kGuaranteedDestroys = 4;

    // The most destroys one snapshot carries. Paced rather than dropped -- the
    // debt is owed until the peer confirms it -- because a sweep that emptied a
    // zone would otherwise spend a whole datagram on the news while the player's
    // own entity waited behind it.
    constexpr std::size_t kMaxDestroysPerSnapshot = 32;
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
    const std::span<const NetEntityId> owedDestroys = peer.OwedDestroys();

    // What this peer is owed about each entity, and what saying it would cost.
    std::vector<std::uint64_t> masks;
    masks.reserve(changes.Size() * 2);
    std::vector<PlannedEntity> planned;
    planned.reserve(changes.Size());
    for (const ReplicationChangeStore::EntityState& entity : changes.Live())
    {
        PlannedEntity plan;
        if (PlanEntity(entity, layout, peer, request.OwnerPeer, masks, plan))
            planned.push_back(plan);
    }

    // Whose turn it is. The peer's own entities first, then longest since it
    // last heard about them -- which puts everything it has never been sent at
    // the front of the rest, and that is the case that matters, because a peer
    // joining a world larger than one datagram is owed all of it at once and can
    // only be seeded a datagram at a time. Identity breaks ties so two runs of
    // the same simulation fill the same way.
    std::sort(planned.begin(), planned.end(),
              [](const PlannedEntity& a, const PlannedEntity& b) {
                  if (a.Owned != b.Owned)
                      return a.Owned;
                  if (a.LastSentAt != b.LastSentAt)
                      return a.LastSentAt < b.LastSentAt;
                  return a.Entity->Id.Value < b.Entity->Id.Value;
              });

    const std::size_t capacityBits = out.size() * 8;
    if (capacityBits < kHeaderBits)
        return result;  // Smaller than an empty snapshot; nothing can be said.
    const std::size_t bodyBits = capacityBits - kHeaderBits;

    // Room set aside for the peer's own entities before destroys take theirs, so
    // that emptying a zone cannot cost a player the correction their prediction
    // is waiting on. Capped so destroys keep a few records of their own: whoever
    // is served first must not be served exclusively.
    std::size_t ownedBits = 0;
    for (const PlannedEntity& plan : planned)
    {
        if (!plan.Owned)
            break;  // Sorted ahead of everything else.
        if (ownedBits + plan.Bits > bodyBits)
            break;
        ownedBits += plan.Bits;
    }
    // What the destroys that are guaranteed room actually cost, so the
    // reservation above cannot claim the space they are promised.
    std::size_t guaranteed = 0;
    for (std::size_t i = 0;
         i < owedDestroys.size() && i < kGuaranteedDestroys; ++i)
    {
        guaranteed += NetEntityIdBits(owedDestroys[i]);
    }
    guaranteed = std::min(bodyBits, guaranteed);
    const std::size_t reserved = std::min(ownedBits, bodyBits - guaranteed);

    // Destroys go in ahead of everything the reservation did not claim. An
    // update that waits is superseded by the next one, so waiting costs it
    // freshness; a destroy that waits leaves a client holding an entity that is
    // not there, which nothing later corrects except the destroy itself. Oldest
    // first, so the tail of a large sweep does not sit behind whatever died
    // since.
    std::size_t destroysWritten = 0;
    std::size_t destroyBits = 0;
    while (destroysWritten < owedDestroys.size()
           && destroysWritten < kMaxDestroysPerSnapshot)
    {
        const std::size_t cost = NetEntityIdBits(owedDestroys[destroysWritten]);
        if (destroyBits + cost > bodyBits - reserved)
            break;
        destroyBits += cost;
        ++destroysWritten;
    }
    const std::span<const NetEntityId> destroyed =
        owedDestroys.subspan(0, destroysWritten);
    result.DestroysDeferred =
        static_cast<std::uint32_t>(owedDestroys.size() - destroysWritten);

    std::size_t used = kHeaderBits + destroyBits;

    // What fits, in that order. An entity is taken whole or not at all: a first
    // send carries the entity's every field, and the client turns that into a
    // spawn, so half of one is not a smaller spawn but a wrong one.
    const std::size_t entityCap = ReplicationDefaultCaps().MaxEntitiesPerSnapshot;
    std::vector<const PlannedEntity*> sending;
    sending.reserve(std::min(planned.size(), entityCap));
    for (const PlannedEntity& plan : planned)
    {
        // Larger than an empty snapshot, so no amount of waiting will find it
        // room. Skipped rather than allowed to block everything behind it, and
        // counted, because an entity that can never be described is a fault
        // somebody has to be told about rather than back-pressure.
        if (kHeaderBits + plan.Bits > capacityBits)
        {
            ++result.EntitiesUnsendable;
            continue;
        }
        if (sending.size() >= entityCap || used + plan.Bits > capacityBits)
        {
            // Deferred, not dropped: nothing about it is recorded as sent, so it
            // stays owed and keeps its place at the front of the next snapshot.
            ++result.EntitiesDeferred;
            // One it has never been sent has been waiting since this peer's
            // first snapshot, not since sequence zero -- a peer that joined a
            // moment ago is not decades behind.
            const std::uint32_t since = plan.LastSentAt != 0
                                            ? plan.LastSentAt
                                            : peer.FirstSequence();
            if (request.Sequence > since)
            {
                result.OldestDeferredSnapshots = std::max(
                    result.OldestDeferredSnapshots, request.Sequence - since);
            }
            continue;
        }
        used += plan.Bits;
        sending.push_back(&plan);
    }

    NetBitWriter writer(out);
    writer.WriteU64(request.Tick);
    writer.WriteBits(request.Sequence, kSequenceBits);
    writer.WriteU64(request.CommandAck);
    writer.WriteBits(static_cast<std::uint32_t>(destroyed.size()), kCountBits);
    writer.WriteBits(static_cast<std::uint32_t>(sending.size()), kCountBits);

    for (NetEntityId id : destroyed)
        WriteNetEntityId(writer, id);

    for (const PlannedEntity* plan : sending)
    {
        const ReplicationChangeStore::EntityState& entity = *plan->Entity;

        WriteNetEntityId(writer, entity.Id);
        writer.WriteBits(static_cast<std::uint32_t>(entity.Components.size()),
                         kComponentCountBits);

        for (std::size_t i = 0; i < entity.Components.size(); ++i)
        {
            const ReplicationChangeStore::ComponentState& held = entity.Components[i];
            const ReplicatedComponent* component = layout.At(held.WireIndex);
            writer.WriteBits(held.WireIndex, kComponentIndexBits);

            if (!ReplicationEncodeComponent(*component, held.Bytes,
                                            masks[plan->FirstMask + i], writer))
            {
                return result;
            }
        }
    }

    // The fill arithmetic and the encoder have to agree exactly, or a snapshot
    // that was measured to fit overflows and the whole peer goes unserved. They
    // are separate code reading the same layout, which is precisely the shape
    // that drifts silently.
    assert(writer.BitsWritten() == used
           && "snapshot cost was measured differently than it was written");
    if (writer.Overflowed())
        return result;

    // Recorded only now the whole snapshot has encoded: a record written as each
    // entity went in would, on a snapshot abandoned part way, claim a peer was
    // sent state that never left the building.
    peer.RecordDestroysSent(request.Sequence, destroyed);
    for (const PlannedEntity* plan : sending)
        peer.RecordSent(request.Sequence, plan->Entity->Id, generation);

    result.Ok = true;
    result.BytesWritten = writer.BytesWritten();
    result.EntitiesWritten = static_cast<std::uint32_t>(sending.size());
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
