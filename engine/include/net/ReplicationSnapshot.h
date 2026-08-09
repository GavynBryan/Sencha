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
    // Snapshots that may be in flight before a peer that has stopped
    // acknowledging is treated as new and told everything again. Well past any
    // round trip a session tolerates; a peer further behind than this has a
    // connection whose deltas are not worth computing.
    static constexpr std::size_t kMaxUnacknowledged = 32;

    // How far through the authority's history this peer has been carried for
    // one entity: the newest generation it has proved it holds. Zero means it
    // has never confirmed anything about the entity, so everything is owed.
    [[nodiscard]] std::uint64_t Floor(NetEntityId id) const;
    [[nodiscard]] bool Knows(NetEntityId id) const;
    [[nodiscard]] std::size_t Size() const { return Floors.size(); }

    // The name the next snapshot for this peer goes out under. One per snapshot
    // written, never reused, and not the simulation tick: a frame can publish
    // twice at one tick, and two datagrams sharing a name cannot be told apart
    // by an acknowledgement.
    [[nodiscard]] std::uint32_t NextSnapshotSequence() const { return NextSequence; }

    // Opens a snapshot under that sequence, and enforces the bound: a peer with
    // too many outstanding is one whose confirmations have stopped arriving, so
    // what it is believed to hold is forgotten and it is told everything again.
    void BeginSnapshot(std::uint32_t sequence);

    // Records that a snapshot carried this entity at that generation, pending
    // proof it arrived.
    void RecordSent(std::uint32_t sequence, NetEntityId id,
                    std::uint64_t generation);

    // Everything a destroyed entity had, in flight or confirmed.
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

    [[nodiscard]] const std::unordered_map<NetEntityId, std::uint64_t>& All() const
    {
        return Floors;
    }

private:
    // What one snapshot told this peer: which entities, and how far through the
    // authority's history each was carried.
    struct SentSnapshot
    {
        std::uint32_t Sequence = 0;
        std::vector<std::pair<NetEntityId, std::uint64_t>> Entities;
    };

    std::unordered_map<NetEntityId, std::uint64_t> Floors;
    // Oldest first, one entry per snapshot still unconfirmed.
    std::vector<SentSnapshot> Pending;
    // Starts at one, because zero is the acknowledgement's "nothing yet".
    std::uint32_t NextSequence = 1;
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
    std::size_t BytesWritten = 0;
};

// Encodes one snapshot into `out`. Returns what it did, so a caller can log or
// budget without re-deriving it.
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
    std::uint32_t EntitiesUpdated = 0;
    std::uint32_t EntitiesDestroyed = 0;
    // The predicted pawn's authoritative state was updated by this snapshot, so
    // the caller has something new to reconcile against. Deciding what to do
    // about it is not a snapshot applier's business.
    bool PredictedStateUpdated = false;

    [[nodiscard]] bool Ok() const { return Error == SnapshotApplyError::None; }
};

// Decodes a snapshot onto the target world. Structural work -- creating an
// entity for an identity not seen before, destroying one the snapshot says is
// gone -- happens here, so this must be called where structural mutation is
// legal: outside any active query, at the start of the client's tick.
[[nodiscard]] SnapshotApplyResult ReplicationApplySnapshot(
    const SnapshotApplyRequest& request,
    std::span<const std::byte> bytes);
