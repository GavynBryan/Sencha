#pragma once

#include <core/identity/StrongId.h>
#include <ecs/EntityId.h>
#include <net/NetSpawnRecipe.h>
#include <net/ClientPrediction.h>
#include <net/NetSnapshotAck.h>
#include <net/ReplicationCodec.h>
#include <net/ReplicationLayout.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

class ReplicationChangeStore;
class ReplicationInterpolation;
class World;
class WorldComponentSchema;

//=============================================================================
// ReplicationSnapshot
//
// What the authority sends and what a client applies. The authority walks the
// entities marked for replication, encodes each one's replicated components
// against what it believes that client already holds, and sends the difference;
// the client decodes it onto its own world.
//
// Neither side ever sees the other's EntityId. A runtime entity handle is an
// index plus a generation into one particular World and means nothing anywhere
// else, so identity on the wire is a NetEntityId the authority mints, and each
// side keeps its own map.
//=============================================================================

// Session-transient entity identity, minted by the authority. Every replicated
// entity gets one, including authored ones.
//
// Resolving authored entities through their PersistentEntityId instead -- which
// would let a snapshot describe the level's contents with no spawn messages at
// all -- is the intended optimization and is deliberately not taken here. It
// needs a namespace that cannot collide with the runtime persistent-mint
// allocator that bit 63 of PersistentEntityId is reserved for, and inventing
// one before that allocator exists would be guessing at its shape.
using NetEntityId = StrongId<struct NetEntityIdTag, std::uint64_t>;

//-----------------------------------------------------------------------------
// What one side remembers between snapshots.
//
// On the authority this is per client: how far through the authority's own
// history each entity has been carried to that client, and which snapshots are
// still waiting to be confirmed. On a client it is the one map from wire
// identity to its own entities.
//
// No component bytes live here. What the world looks like has one answer for
// everyone (ReplicationChangeStore holds it), so a peer only needs to remember
// how much of that answer it has proved it received -- a generation per entity.
// Keeping per-peer copies meant the same comparison run once per peer and the
// memory to run it with, both of which scale with exactly the number this work
// exists to raise.
//
// "Proved", not "was sent", and the distinction is the whole reason this class
// is more than a map. Snapshots ride an unreliable channel: one that is dropped
// was still written, and a floor advanced on writing would from then on
// describe a peer as up to date with history it never saw. Every later
// difference is measured from that fiction, so anything that does not happen to
// move again is never sent and the two machines disagree about it permanently.
//-----------------------------------------------------------------------------
class ReplicationPeerState
{
public:
    // Snapshots that may be waiting on proof at once. Well past any round trip a
    // session tolerates, and past the acknowledgement window as well, so the
    // bound is only ever reached by a peer that has stopped answering
    // altogether. Reaching it drops the oldest one unresolved, which costs the
    // bandwidth to describe what it carried again and never a floor: what a peer
    // has proved it holds stays proved however long it then goes quiet.
    static constexpr std::size_t kMaxUnacknowledged = 32;

    // What a peer has been told about one entity.
    struct EntityRecord
    {
        // The newest generation it has proved it holds. Zero means it has
        // confirmed nothing about the entity, so everything is owed.
        std::uint64_t Floor = 0;
        // The last snapshot this entity was written into, proved or not.
        // Separate from the floor because it answers a different question: the
        // floor decides what to say, this decides who gets said first when there
        // is not room for everyone. An entity written into each of the last
        // three snapshots and confirmed in none of them is still owed
        // everything, but it is not the one going hungry.
        std::uint32_t LastSentAt = 0;
    };

    // How far through the authority's history this peer has been carried for
    // one entity. Zero for an entity it has never confirmed anything about.
    [[nodiscard]] std::uint64_t Floor(NetEntityId id) const;
    // Whether this peer has ever confirmed anything about the entity. Not the
    // same as having been sent it: a snapshot that was written may have been
    // dropped, and a peer that has confirmed nothing may not have the entity at
    // all, which is why the spawn stays owed until it says otherwise.
    [[nodiscard]] bool Knows(NetEntityId id) const;
    // Which snapshot last carried it, or zero if none ever has.
    [[nodiscard]] std::uint32_t LastSentAt(NetEntityId id) const;
    // The first snapshot ever opened for this peer, which is when everything it
    // has not been sent started waiting. Zero before the first one.
    [[nodiscard]] std::uint32_t FirstSequence() const { return First; }
    [[nodiscard]] std::size_t Size() const { return Entities.size(); }

    // The name the next snapshot for this peer goes out under. One per snapshot
    // written, never reused, and not the simulation tick: a frame can publish
    // twice at one tick, and two datagrams sharing a name cannot be told apart
    // by an acknowledgement.
    [[nodiscard]] std::uint32_t NextSnapshotSequence() const { return NextSequence; }

    // Opens a snapshot under that sequence, and enforces the bound above.
    void BeginSnapshot(std::uint32_t sequence);

    // Records that a snapshot carried this entity at that generation, pending
    // proof it arrived.
    void RecordSent(std::uint32_t sequence, NetEntityId id,
                    std::uint64_t generation);

    // An entity this peer was told about that the world no longer has. Held as
    // a debt rather than acted on: the peer stops being up to date with it at
    // once, but it is owed the news until it confirms hearing it. A destroy
    // said once and forgotten leaves a client holding an entity that will never
    // be mentioned again, because the authority has nothing left to mention.
    void NoteDeparted(NetEntityId id);
    [[nodiscard]] std::span<const NetEntityId> OwedDestroys() const
    {
        return { Departed.data(), Departed.size() };
    }

    // Records that a snapshot carried these destroys, pending proof.
    void RecordDestroysSent(std::uint32_t sequence,
                            std::span<const NetEntityId> ids);

    // Everything about an entity, in flight or confirmed. For a peer leaving,
    // not for an entity dying -- that is NoteDeparted.
    void Forget(NetEntityId id);

    // Applies what the peer has proved it holds. A pending snapshot raises
    // floors only when the acknowledgement names it: a cumulative mark would
    // vouch for the ones lost on the way, recording the peer as up to date with
    // history it never received. One the window has passed without naming is
    // dropped rather than applied -- what it carried stays owed, which costs
    // bandwidth and never correctness.
    void Acknowledge(const NetSnapshotAck& ack);

    [[nodiscard]] std::size_t Unacknowledged() const { return Pending.size(); }

    void Clear();

    [[nodiscard]] const std::unordered_map<NetEntityId, EntityRecord>& All() const
    {
        return Entities;
    }

private:
    // What one snapshot told this peer: which entities, and how far through the
    // authority's history each was carried.
    struct SentSnapshot
    {
        std::uint32_t Sequence = 0;
        std::vector<std::pair<NetEntityId, std::uint64_t>> Entities;
        std::vector<NetEntityId> Destroyed;
    };

    std::unordered_map<NetEntityId, EntityRecord> Entities;
    // Destroys this peer is owed, oldest first. Cleared only by proof.
    std::vector<NetEntityId> Departed;
    // Oldest first, one entry per snapshot still unconfirmed.
    std::vector<SentSnapshot> Pending;
    // Starts at one, because zero is the acknowledgement's "nothing yet".
    std::uint32_t NextSequence = 1;
    std::uint32_t First = 0;
};

//-----------------------------------------------------------------------------
// The authority's identity mint and its map into its own world.
//-----------------------------------------------------------------------------
class ReplicationAuthorityIdentity
{
public:
    // Stable across calls for the same entity, so an entity keeps one identity
    // for as long as it lives.
    [[nodiscard]] NetEntityId IdFor(EntityId entity);
    [[nodiscard]] NetEntityId TryFind(EntityId entity) const;
    void Release(EntityId entity);
    // Drops identities for entities the world no longer has. Without this the
    // map grows for the life of the session: an entity that is destroyed is
    // never named again, so nothing else would ever remove it.
    void ForgetDead(const World& world);

    [[nodiscard]] std::size_t Size() const { return Forward.size(); }

private:
    std::unordered_map<EntityId, NetEntityId, EntityIdHash> Forward;
    // Starts at one: zero is the strong id's invalid sentinel.
    std::uint64_t NextId = 1;
};

//-----------------------------------------------------------------------------
// A client's map from wire identity to the entities it created for them.
//-----------------------------------------------------------------------------
class ReplicationClientIdentity
{
public:
    [[nodiscard]] EntityId TryResolve(NetEntityId id) const;
    void Bind(NetEntityId id, EntityId entity);
    void Unbind(NetEntityId id);
    void Clear() { Entries.clear(); }

    [[nodiscard]] std::size_t Size() const { return Entries.size(); }
    [[nodiscard]] const std::unordered_map<NetEntityId, EntityId>& All() const
    {
        return Entries;
    }

private:
    std::unordered_map<NetEntityId, EntityId> Entries;
};

//-----------------------------------------------------------------------------
// Bounds. Every count a decoder reads is checked against one of these before it
// is used to size or index anything.
//-----------------------------------------------------------------------------
struct ReplicationCaps
{
    std::uint32_t MaxEntitiesPerSnapshot = 1024;
    std::uint32_t MaxComponentsPerEntity = 32;
};

//-----------------------------------------------------------------------------
// The snapshot envelope's field widths, in bits.
//
// Stated here rather than only inside the writer because they are the wire
// contract: anything that builds or inspects a snapshot without going through
// the writer reads them from one place instead of restating them and drifting.
//
// Counts are as wide as the cap they are checked against and no wider. Entity
// identities are not here at all: they are written as a variable width, because
// they are minted from one and never reused, so most of a session's identities
// are small and a fixed sixty-four bits was most of what an entity's envelope
// cost.
//-----------------------------------------------------------------------------
struct ReplicationSnapshotWire
{
    static constexpr std::uint8_t TickBits = 64;
    static constexpr std::uint8_t SequenceBits = 32;
    static constexpr std::uint8_t CommandAckBits = 64;
    static constexpr std::uint8_t CountBits = 11;
    static constexpr std::uint8_t ComponentCountBits = 8;
    static constexpr std::uint8_t ComponentIndexBits = 8;
};

[[nodiscard]] const ReplicationCaps& ReplicationDefaultCaps();

//-----------------------------------------------------------------------------
// Writing
//-----------------------------------------------------------------------------
struct SnapshotWriteRequest
{
    // What the world looks like and when each part of it last moved, gathered
    // once for every peer rather than once per peer.
    const ReplicationChangeStore* Changes = nullptr;
    const ReplicationLayout* Layout = nullptr;
    // Updated in place to reflect what this snapshot told the peer. A caller
    // that discards the produced bytes must discard this too, or the next
    // delta will be against a snapshot the peer never received.
    ReplicationPeerState* Peer = nullptr;
    // Whose owner-only fields to include. Zero means no peer owns anything
    // here, which is what a spectator or a recording gets.
    std::uint32_t OwnerPeer = 0;
    std::uint64_t Tick = 0;
    // This snapshot's name for the peer it is written for. Taken from the peer
    // state, which mints them in order.
    std::uint32_t Sequence = 0;
    // The newest command tick from this peer that the authority has finished
    // with. A client keeps the ticks it has simulated and not had answered; the
    // ones at or below this are answered, and everything above them is what a
    // replay owes. Without it a client cannot tell which of its own guesses the
    // state it just received already accounts for.
    std::uint64_t CommandAck = 0;
};

struct SnapshotWriteResult
{
    bool Ok = false;
    std::uint32_t EntitiesWritten = 0;
    std::uint32_t EntitiesDestroyed = 0;
    // Entities that had something to say and were left for a later snapshot.
    // Back-pressure, not loss: nothing about them was recorded as sent, so the
    // next snapshot still owes exactly the same difference, and their turn comes
    // before the ones that just went out.
    std::uint32_t EntitiesDeferred = 0;
    // Entities that would not fit a snapshot of their own. Counted apart from
    // the ones above because waiting will not help: nothing about them can ever
    // reach this peer, so this is a budget or layout fault rather than a busy
    // frame, and it is the one back-pressure case that needs an operator.
    std::uint32_t EntitiesUnsendable = 0;
    std::uint32_t DestroysDeferred = 0;
    // Snapshots the longest-waiting deferred entity has gone without being
    // carried. Counted from this peer's first snapshot for one it has never been
    // sent, so a peer still being seeded reads as "waiting since it joined"
    // rather than as waiting since the session began.
    std::uint32_t OldestDeferredSnapshots = 0;
    std::size_t BytesWritten = 0;
};

// Encodes one snapshot into `out`, filling it as far as it goes: `out` is the
// budget, not a buffer that is assumed large enough. What does not fit is
// deferred whole entities at a time and reported, never a part-written one.
//
// Succeeds for any world. An authority that could not describe its world inside
// one datagram used to write nothing at all, which sounds conservative and is
// the opposite: a peer with no proven state is owed every entity at once, so the
// snapshot that would seed it is the largest one it will ever be sent, and
// failing to send it means it is never seeded and therefore never owed less.
// Past a few dozen entities a joining peer starved permanently.
[[nodiscard]] SnapshotWriteResult ReplicationWriteSnapshot(
    const SnapshotWriteRequest& request,
    std::span<std::byte> out);

//-----------------------------------------------------------------------------
// Applying
//-----------------------------------------------------------------------------
enum class SnapshotApplyError : std::uint8_t
{
    None = 0,
    Truncated,
    CapExceeded,
    // A component key this build does not define. The identity gate should have
    // refused the session before this could happen, so it is a protocol
    // violation rather than a version difference to tolerate.
    UnknownComponent,
    // The receiving world has no column for a component the snapshot names.
    UnknownComponentStorage,
    // A component arrived for an entity that does not carry it and could not be
    // given it.
    ComponentAddFailed,
};

[[nodiscard]] std::string_view SnapshotApplyErrorToString(SnapshotApplyError error);

struct SnapshotApplyRequest
{
    World* Target = nullptr;
    const WorldComponentSchema* Schema = nullptr;
    const ReplicationLayout* Layout = nullptr;
    ReplicationClientIdentity* Identity = nullptr;
    // What this machine simulates for itself, or null when it mirrors
    // everything. A predicted entity's position is handed here rather than
    // written, because the world's copy is what this machine produced and the
    // arriving one is the authority's argument with it.
    ClientPrediction* Prediction = nullptr;
    // What this machine mirrors rather than simulates, or null to write arriving
    // poses straight to the world. A mirrored entity's pose is handed here
    // rather than written, because the tick it describes is not the tick this
    // machine is about to draw.
    ReplicationInterpolation* Interpolation = nullptr;
    // What a newly spawned entity becomes beyond its replicated state. Null
    // means an entity is only what the wire said, which leaves it with no
    // derived columns and nothing to draw it -- valid for a test, wrong for a
    // game.
    const NetSpawnRecipes* Recipes = nullptr;
    // Where newly spawned entities are created. Partition zero is the
    // persistent one and is where a session's pawns live.
    std::uint16_t Partition = 0;
};

struct SnapshotApplyResult
{
    // The authority's name for this snapshot, which the client reports back as
    // proof it arrived. Per peer and unique per snapshot, unlike the tick.
    std::uint32_t Sequence = 0;
    SnapshotApplyError Error = SnapshotApplyError::None;
    std::uint64_t Tick = 0;
    // What the authority has finished simulating of this client's own input.
    std::uint64_t CommandAck = 0;
    std::uint32_t EntitiesSpawned = 0;
    // Spawns that named a recipe this build does not have. Not an error -- an
    // authority may run content a client did not register -- but the entity is
    // bare, so it is counted rather than silently dropped.
    std::uint32_t RecipesMissing = 0;
    // Which recipe the first of those named. Carried because the count alone
    // cannot be acted on: this failure looks like an entity that is in the
    // right place with the right state and nothing drawing it, and the one
    // thing that shortens the search is the number the authority asked for.
    NetSpawnRecipeId FirstMissingRecipe = kNetNoSpawnRecipe;
    std::uint32_t EntitiesUpdated = 0;
    std::uint32_t EntitiesDestroyed = 0;
    // Whether this snapshot should drive a reconcile of the predicted pawn.
    // True for every snapshot a predicting client applies, not only ones that
    // carried the pawn's own state: the shadow holds the authority's last word
    // whether or not this snapshot refreshed it, and a pawn the authority has
    // not moved is exactly the case a client can be wrong about on its own.
    // Gating on the pawn appearing would mean a client that predicted movement
    // the authority never performed is never told.
    bool ReconcilePredicted = false;

    [[nodiscard]] bool Ok() const { return Error == SnapshotApplyError::None; }
};

// Decodes a snapshot onto the target world. Structural work -- creating an
// entity for an identity not seen before, destroying one the snapshot says is
// gone -- happens here, so this must be called where structural mutation is
// legal: outside any active query, at the start of the client's tick.
[[nodiscard]] SnapshotApplyResult ReplicationApplySnapshot(
    const SnapshotApplyRequest& request,
    std::span<const std::byte> bytes);
