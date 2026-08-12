#include <net/ReplicationSnapshot.h>

#include <net/ReplicationChangeStore.h>

#include <ecs/Query.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <net/NetReplicationComponents.h>
#include <net/ReplicationInterpolation.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/transform/DerivedTransform.h>

#include <algorithm>
#include <cassert>
#include <cstring>

namespace
{
    //-------------------------------------------------------------------------
    // A decoded snapshot, before any of it has been applied.
    //
    // A snapshot is read whole and then written whole. The two halves are split
    // because they fail differently: reading is where a peer's bytes can be
    // wrong, and writing is where the world changes, and those must not be the
    // same pass. They were, and a truncation partway through left entities
    // already destroyed, others already created, and a third half-written --
    // with nothing to undo any of it.
    //
    // The created ones were the lasting damage. An entity bound to an identity
    // but never finished is not spawned again on the next snapshot, so it never
    // runs its recipe and never gains a world transform: correct in state,
    // invisible on screen, for the rest of the session.
    //
    // So the read half may fail anywhere, having touched neither the world nor
    // the identity map, and the write half cannot fail at all.
    //-------------------------------------------------------------------------
    enum class SnapshotSink : std::uint8_t
    {
        // Straight onto the entity.
        World,
        // Held for the predictor to argue with what this machine simulated.
        Prediction,
        // Held with the tick it describes, to be presented later.
        Interpolation,
    };

    struct PlannedComponent
    {
        const ReplicatedComponent* Layout = nullptr;
        SnapshotSink Sink = SnapshotSink::World;
        // Where this component's decoded bytes start in the plan's arena.
        std::size_t Offset = 0;
        // Whether the entity will already carry the column when this is
        // written: overwrite in place, or add. Decided while reading, which is
        // sound because reading changes nothing that could make it wrong.
        bool PresentInWorld = false;
    };

    struct PlannedEntityUpdate
    {
        NetEntityId Id;
        // The entity this lands on, or invalid when the write half has to
        // create one.
        EntityId Local;
        bool Spawned = false;
        std::size_t FirstComponent = 0;
        std::uint32_t ComponentCount = 0;
        // Components the entity no longer carries, as wire keys into the
        // layout. Indices rather than pointers, into the plan's own list.
        std::size_t FirstRemoval = 0;
        std::uint32_t RemovalCount = 0;
        // The wire named an authored entity this machine has not loaded yet.
        // Its bytes are read so the stream stays aligned and then dropped, and
        // the snapshot is reported incomplete so it is never acknowledged --
        // which is what keeps the authority's floor where it was and makes the
        // entity arrive again.
        bool Deferred = false;
        // Bind the identity at commit. True for an authored entity recognised
        // through the index rather than created here.
        bool BindIdentity = false;
    };

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
    constexpr std::uint8_t kAuthoredPresentBits =
        ReplicationSnapshotWire::AuthoredPresentBits;
    constexpr std::uint8_t kRemovalsPresentBits =
        ReplicationSnapshotWire::RemovalsPresentBits;

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

    // What a variable-width value costs on the wire, which the fill has to know
    // before it writes one.
    std::size_t VarUIntBits(std::uint64_t value)
    {
        std::size_t groups = 1;
        for (std::uint64_t rest = value >> 7; rest != 0; rest >>= 7)
            ++groups;
        return groups * 8;
    }

    // Not a constant any more: an identity is as wide as it needs to be.
    std::size_t NetEntityIdBits(NetEntityId id)
    {
        return VarUIntBits(id.Value);
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
    Reverse.emplace(minted, entity);
    return minted;
}

EntityId ReplicationAuthorityIdentity::TryResolve(NetEntityId id) const
{
    const auto it = Reverse.find(id);
    return it == Reverse.end() ? EntityId{} : it->second;
}

NetEntityId ReplicationAuthorityIdentity::TryFind(EntityId entity) const
{
    const auto it = Forward.find(entity);
    return it == Forward.end() ? NetEntityId{} : it->second;
}

void ReplicationAuthorityIdentity::Release(EntityId entity)
{
    const auto it = Forward.find(entity);
    if (it == Forward.end())
        return;
    Reverse.erase(it->second);
    Forward.erase(it);
}

void ReplicationAuthorityIdentity::ForgetDead(const World& world)
{
    std::erase_if(Forward, [&](const auto& entry) {
        if (world.IsAlive(entry.first))
            return false;
        Reverse.erase(entry.second);
        return true;
    });
}

EntityId ReplicationClientIdentity::TryResolve(NetEntityId id) const
{
    const auto it = Entries.find(id);
    return it == Entries.end() ? EntityId{} : it->second;
}

NetEntityId ReplicationClientIdentity::TryFind(EntityId entity) const
{
    const auto it = Names.find(entity);
    return it == Names.end() ? NetEntityId{} : it->second;
}

void ReplicationClientIdentity::Bind(NetEntityId id, EntityId entity)
{
    // Rebinding an identity onto a different entity -- which is what
    // recognising an authored copy does -- drops the old pairing from both
    // directions, so nothing goes on answering to a name it has lost.
    const auto existing = Entries.find(id);
    if (existing != Entries.end())
        Names.erase(existing->second);

    Entries[id] = entity;
    Names[entity] = id;
}

void ReplicationClientIdentity::Unbind(NetEntityId id)
{
    const auto it = Entries.find(id);
    if (it == Entries.end())
        return;
    Names.erase(it->second);
    Entries.erase(it);
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
        // Whether this record carries the entity's authored identity.
        bool SendAuthored = false;
        // How many of the entity's recorded removals this peer has not been
        // told about. They are the leading ones: a removal is recorded with the
        // generation it happened at, and the list is only ever appended to.
        std::uint32_t OwedRemovals = 0;
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
        // One bit for whether anything was removed, and the count only when
        // something was. A component leaving is rare and an entity is common,
        // so a byte-wide count on every entity of every snapshot costs more
        // over a seeding datagram than the removals it describes ever will.
        std::size_t bits = NetEntityIdBits(entity.Id) + kComponentCountBits
                         + kRemovalsPresentBits;

        // Whether the authored identity rides with this record. Sent while the
        // peer has confirmed nothing about the entity, which is exactly while
        // it might still have to recognise its own copy rather than build one.
        // The bit itself is unconditional: a reader cannot infer what a writer
        // knew about its floor, and a bit the two disagree about is the rest of
        // the snapshot read at the wrong offset.
        const bool sendAuthored = entity.Persistent.IsValid() && !peer.Knows(entity.Id);
        bits += kAuthoredPresentBits;
        if (sendAuthored)
            bits += VarUIntBits(entity.Persistent.Value);

        bool owedAnything = false;

        // A component this peer was shown and has not been told is gone. Owed
        // to a peer whose floor predates the removal, which is also every peer
        // that has never heard of the entity -- harmless, because a client
        // removing a component it does not have is already true.
        std::uint32_t owedRemovals = 0;
        for (const ReplicationChangeStore::RemovedComponent& gone : entity.Removed)
        {
            if (gone.RemovedAt <= floor)
                continue;
            ++owedRemovals;
            bits += kComponentIndexBits;
        }
        if (owedRemovals != 0)
            bits += kComponentCountBits;
        owedAnything = owedAnything || owedRemovals != 0;

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
            .SendAuthored = sendAuthored,
            .OwedRemovals = owedRemovals,
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
        writer.WriteBool(plan->SendAuthored);
        if (plan->SendAuthored)
            writer.WriteVarUInt(entity.Persistent.Value);
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

        // Then what the entity stopped carrying. After the components rather
        // than before them, so a component removed and added back inside one
        // peer's floor arrives as an add: the wire says what the entity has,
        // and then what it does not.
        writer.WriteBool(plan->OwedRemovals != 0);
        if (plan->OwedRemovals != 0)
        {
            writer.WriteBits(plan->OwedRemovals, kComponentCountBits);
            const std::uint64_t floor = peer.Floor(entity.Id);
            for (const ReplicationChangeStore::RemovedComponent& gone : entity.Removed)
            {
                if (gone.RemovedAt <= floor)
                    continue;
                writer.WriteBits(gone.WireIndex, kComponentIndexBits);
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

    //-------------------------------------------------------------------------
    // Read. Nothing below here touches the world or the identity map.
    //-------------------------------------------------------------------------
    std::vector<NetEntityId> destroyed;
    destroyed.reserve(destroyedCount);
    for (std::uint32_t i = 0; i < destroyedCount; ++i)
    {
        NetEntityId id;
        if (!ReadNetEntityId(reader, id))
        {
            result.Error = SnapshotApplyError::Truncated;
            return result;
        }
        destroyed.push_back(id);
    }

    // Sorted so an update can ask whether its identity is one this same
    // snapshot releases, without a scan per entity. The writer never names an
    // entity in both lists; a peer that does would otherwise have its update
    // resolve to an entity the destroy pass is about to remove, and the write
    // half would land on a dead handle.
    std::vector<NetEntityId> releasing = destroyed;
    std::sort(releasing.begin(), releasing.end(),
              [](NetEntityId a, NetEntityId b) { return a.Value < b.Value; });
    const auto isReleasing = [&releasing](NetEntityId id) {
        return std::binary_search(
            releasing.begin(), releasing.end(), id,
            [](NetEntityId a, NetEntityId b) { return a.Value < b.Value; });
    };

    std::vector<PlannedEntityUpdate> planned;
    std::vector<PlannedComponent> plannedComponents;
    std::vector<const ReplicatedComponent*> plannedRemovals;
    // One arena for every component's decoded bytes, so the write half reads
    // from a single allocation rather than one per component.
    std::vector<std::byte> decoded;
    planned.reserve(updatedCount);

    for (std::uint32_t i = 0; i < updatedCount; ++i)
    {
        // Field order is the wire contract: identity, then the authored key,
        // then the component count. Read in the order the writer writes them.
        NetEntityId id;
        bool hasAuthored = false;
        std::uint64_t authored = 0;
        std::uint32_t componentCount = 0;
        if (!ReadNetEntityId(reader, id)
            || !reader.ReadBool(hasAuthored)
            || (hasAuthored && !reader.ReadVarUInt(authored))
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

        PlannedEntityUpdate update;
        update.Id = id;
        update.Local = identity.TryResolve(id);
        update.Spawned = isReleasing(id) || !update.Local.IsValid()
                      || !world.IsAlive(update.Local);
        if (update.Spawned)
            update.Local = EntityId{};

        // An authored entity is already here under its own identity, because
        // this machine loaded the same level. Recognising it is the difference
        // between a door and two doors.
        if (update.Spawned && hasAuthored)
        {
            const PersistentEntityIndex* index =
                world.TryGetResource<PersistentEntityIndex>();
            const EntityId existing =
                index == nullptr ? EntityId{}
                                 : index->TryResolve(PersistentEntityId{ authored });
            if (existing.IsValid() && world.IsAlive(existing))
            {
                update.Local = existing;
                update.Spawned = false;
                update.BindIdentity = true;
                ++result.AuthoredBound;
            }
            else
            {
                // The level has not finished loading, or this build does not
                // have the entity. Creating one would be the duplicate this
                // exists to avoid, so the record is read and dropped.
                update.Deferred = true;
                ++result.AuthoredDeferred;
            }
        }
        update.FirstComponent = plannedComponents.size();
        update.ComponentCount = componentCount;

        const EntityId entity = update.Local;
        const bool spawned = update.Spawned;

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
            // Which of this entity's already-planned components this one
            // repeats, if any. The writer never names a component twice for one
            // entity; a peer that does gets what it would have got when applies
            // were immediate -- the second decoded against the first's result,
            // and the first's write already there.
            const PlannedComponent* earlier = nullptr;
            for (std::size_t p = update.FirstComponent; p < plannedComponents.size(); ++p)
            {
                if (plannedComponents[p].Layout == component)
                    earlier = &plannedComponents[p];
            }

            PlannedComponent slot;
            slot.Layout = component;
            slot.Offset = decoded.size();
            decoded.resize(slot.Offset + component->Size);
            // Taken after the resize, which is why nothing holds one across it.
            const std::span<std::byte> target(decoded.data() + slot.Offset,
                                              component->Size);

            if (request.Prediction != nullptr && !spawned
                && request.Prediction->Intercepts(entity, component->Type))
            {
                slot.Sink = SnapshotSink::Prediction;
                if (request.Prediction->AuthoritativeBytes(component->Type).size()
                    != component->Size)
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
                if (earlier != nullptr)
                {
                    std::memcpy(target.data(), decoded.data() + earlier->Offset,
                                component->Size);
                }
                else if (!request.Prediction->HasAuthoritativeState(component->Type))
                {
                    const ComponentId column =
                        world.GetComponentIdByType(component->Type);
                    const void* held = world.HasComponent(entity, column)
                                           ? world.GetComponentRaw(entity, column)
                                           : nullptr;
                    if (held != nullptr)
                    {
                        std::memcpy(target.data(), held, component->Size);
                    }
                    else if (!schema.WriteDefaultBytes(component->Type, target))
                    {
                        result.Error = SnapshotApplyError::UnknownComponentStorage;
                        return result;
                    }
                }
                else
                {
                    // A delta against what the authority last said, which is
                    // what the shadow already holds.
                    const std::span<const std::byte> shadow =
                        request.Prediction->AuthoritativeBytes(component->Type);
                    std::memcpy(target.data(), shadow.data(), component->Size);
                }
                if (!ReplicationDecodeComponent(*component, reader, target))
                {
                    result.Error = SnapshotApplyError::Truncated;
                    return result;
                }
                plannedComponents.push_back(slot);
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
            const bool ownPawn = request.Prediction != nullptr && !spawned
                              && request.Prediction->Predicts(entity);
            if (request.Interpolation != nullptr && !ownPawn
                && request.Interpolation->Intercepts(component->Type))
            {
                slot.Sink = SnapshotSink::Interpolation;
                // Asked before the shadow is reached for, because reaching for
                // one creates it: a snapshot this pass goes on to refuse must
                // not leave a track behind for an entity it never described.
                const bool held = !spawned
                               && request.Interpolation->HasAuthoritativeState(entity);
                if (earlier != nullptr)
                {
                    std::memcpy(target.data(), decoded.data() + earlier->Offset,
                                component->Size);
                }
                else if (held)
                {
                    const std::span<const std::byte> shadow =
                        request.Interpolation->AuthoritativeBytes(entity);
                    if (shadow.size() != component->Size)
                    {
                        result.Error = SnapshotApplyError::UnknownComponentStorage;
                        return result;
                    }
                    std::memcpy(target.data(), shadow.data(), component->Size);
                }
                else if (!schema.WriteDefaultBytes(component->Type, target))
                {
                    result.Error = SnapshotApplyError::UnknownComponentStorage;
                    return result;
                }
                if (!ReplicationDecodeComponent(*component, reader, target))
                {
                    result.Error = SnapshotApplyError::Truncated;
                    return result;
                }
                slot.PresentInWorld =
                    earlier != nullptr
                    || (!spawned
                        && world.HasComponent(
                               entity, world.GetComponentIdByType(component->Type)));
                plannedComponents.push_back(slot);
                continue;
            }

            slot.Sink = SnapshotSink::World;
            const ComponentId column = world.GetComponentIdByType(component->Type);
            const bool present =
                earlier != nullptr
                || (!spawned && world.HasComponent(entity, column));
            slot.PresentInWorld = present;

            if (earlier != nullptr)
            {
                std::memcpy(target.data(), decoded.data() + earlier->Offset,
                            component->Size);
            }
            else if (!spawned && world.HasComponent(entity, column))
            {
                const void* current = world.GetComponentRaw(entity, column);
                if (current != nullptr)
                    std::memcpy(target.data(), current, component->Size);
                else if (!schema.WriteDefaultBytes(component->Type, target))
                {
                    result.Error = SnapshotApplyError::UnknownComponentStorage;
                    return result;
                }
            }
            else if (!schema.WriteDefaultBytes(component->Type, target))
            {
                result.Error = SnapshotApplyError::UnknownComponentStorage;
                return result;
            }

            if (!ReplicationDecodeComponent(*component, reader, target))
            {
                result.Error = SnapshotApplyError::Truncated;
                return result;
            }
            plannedComponents.push_back(slot);
        }

        bool anyRemoved = false;
        std::uint32_t removalCount = 0;
        if (!reader.ReadBool(anyRemoved))
        {
            result.Error = SnapshotApplyError::Truncated;
            return result;
        }
        if (anyRemoved && !reader.ReadBits(kComponentCountBits, removalCount))
        {
            result.Error = SnapshotApplyError::Truncated;
            return result;
        }
        if (removalCount > caps.MaxComponentsPerEntity)
        {
            result.Error = SnapshotApplyError::CapExceeded;
            return result;
        }
        update.FirstRemoval = plannedRemovals.size();
        update.RemovalCount = removalCount;
        for (std::uint32_t r = 0; r < removalCount; ++r)
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
            plannedRemovals.push_back(component);
        }

        planned.push_back(update);
    }

    //-------------------------------------------------------------------------
    // Write. The snapshot has decoded in full, so nothing here can refuse.
    //-------------------------------------------------------------------------
    for (NetEntityId id : destroyed)
    {
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

    for (const PlannedEntityUpdate& update : planned)
    {
        // Read to keep the stream aligned, and nothing more. The authority
        // still holds it as unconfirmed, so it arrives again once this machine
        // can recognise it.
        if (update.Deferred)
            continue;

        EntityId entity = update.Local;
        if (update.Spawned)
        {
            entity = world.CreateEntity(StoragePartitionId{ request.Partition });
            identity.Bind(update.Id, entity);
            ++result.EntitiesSpawned;
        }
        else
        {
            // An authored entity meeting its wire identity for the first time.
            // The entity is the one the level load made; only the binding is
            // new, which is what keeps its zone, its authored components, and
            // every handle already held on it.
            if (update.BindIdentity)
                identity.Bind(update.Id, entity);
            ++result.EntitiesUpdated;
        }

        for (std::uint32_t c = 0; c < update.ComponentCount; ++c)
        {
            const PlannedComponent& slot =
                plannedComponents[update.FirstComponent + c];
            const ReplicatedComponent& component = *slot.Layout;
            const std::span<const std::byte> value(decoded.data() + slot.Offset,
                                                   component.Size);

            switch (slot.Sink)
            {
            case SnapshotSink::Prediction:
            {
                const std::span<std::byte> shadow =
                    request.Prediction->AuthoritativeBytes(component.Type);
                std::memcpy(shadow.data(), value.data(), component.Size);
                request.Prediction->MarkSeen(component.Type);
                break;
            }
            case SnapshotSink::Interpolation:
            {
                const std::span<std::byte> shadow =
                    request.Interpolation->AuthoritativeBytes(entity);
                std::memcpy(shadow.data(), value.data(), component.Size);
                request.Interpolation->Commit(entity, result.Tick);

                // The component still has to exist, because presenting a pose
                // means writing into it every tick and nothing can be written
                // into a column the entity never gained. Seeded from the
                // authority's own value the first time, so a newly mirrored
                // entity appears where it belongs rather than at the origin and
                // then slides in from there.
                if (!slot.PresentInWorld)
                {
                    const bool added = schema.ImportComponent(world, entity,
                                                              component.Type, value);
                    assert(added && "a component the read half accepted would not add");
                    (void)added;
                }
                break;
            }
            case SnapshotSink::World:
            {
                const bool wrote =
                    slot.PresentInWorld
                        ? schema.SetComponentBytes(world, entity, component.Type, value)
                        : schema.ImportComponent(world, entity, component.Type, value);
                assert(wrote && "a component the read half accepted would not write");
                (void)wrote;
                break;
            }
            }
        }

        // What the entity stopped carrying. After the writes, because the same
        // snapshot can carry a component's new value and another's departure,
        // and the order the wire states is the order the authority meant.
        //
        // A component the entity does not have is not a failure: a peer told
        // about a removal it never saw the addition of is already in the state
        // the message describes.
        for (std::uint32_t r = 0; r < update.RemovalCount; ++r)
        {
            const ReplicatedComponent& gone =
                *plannedRemovals[update.FirstRemoval + r];
            if (world.IsRegistered(gone.Type))
            {
                if (schema.RemoveComponent(world, entity, gone.Type))
                    ++result.ComponentsRemoved;
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
        if (update.Spawned && request.Recipes != nullptr)
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
                if (result.RecipesMissing == 0)
                    result.FirstMissingRecipe = recipeId;
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
