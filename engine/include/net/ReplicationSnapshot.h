#pragma once

#include <core/identity/StrongId.h>
#include <ecs/EntityId.h>
#include <net/NetSpawnRecipe.h>
#include <net/ClientPrediction.h>
#include <net/ReplicationCodec.h>
#include <net/ReplicationLayout.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

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
// On the authority this is per client: which entities that client has been told
// about, and the exact bytes it was last told, so the next snapshot can be a
// difference. On a client it is the one map from wire identity to its own
// entities.
//-----------------------------------------------------------------------------
class ReplicationPeerState
{
public:
    // The component values this peer is believed to hold, already snapped to
    // wire precision so a delta against them is exact.
    struct EntityBaseline
    {
        // Keyed by the component's wire index.
        std::unordered_map<std::uint8_t, std::vector<std::byte>> Components;
    };

    [[nodiscard]] const EntityBaseline* Find(NetEntityId id) const;
    [[nodiscard]] std::size_t Size() const { return Baselines.size(); }

    void Record(NetEntityId id, std::uint8_t component,
                std::span<const std::byte> bytes);
    void Forget(NetEntityId id);
    void Clear() { Baselines.clear(); }

    [[nodiscard]] const std::unordered_map<NetEntityId, EntityBaseline>& All() const
    {
        return Baselines;
    }

private:
    std::unordered_map<NetEntityId, EntityBaseline> Baselines;
};

//-----------------------------------------------------------------------------
// The authority's identity mint and its map into its own world.
//
// EntityId is a generational handle with no std::hash of its own, so the map
// brings its own: index and generation folded together, because a recycled slot
// is a different entity and must not collide with the one it replaced.
//-----------------------------------------------------------------------------
struct EntityIdHash
{
    [[nodiscard]] std::size_t operator()(EntityId entity) const
    {
        return (static_cast<std::size_t>(entity.Index) << 32)
             ^ static_cast<std::size_t>(entity.Generation);
    }
};

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
    // Non-const because a Query binds to a mutable World, not because anything
    // here writes: the writer only reads, and it uses accessors that publish no
    // column version, so running it cannot make the next tick see changes.
    World* Source = nullptr;
    const ReplicationLayout* Layout = nullptr;
    ReplicationAuthorityIdentity* Identity = nullptr;
    // Updated in place to reflect what this snapshot told the peer. A caller
    // that discards the produced bytes must discard this too, or the next
    // delta will be against a snapshot the peer never received.
    ReplicationPeerState* Peer = nullptr;
    // Whose owner-only fields to include. Zero means no peer owns anything
    // here, which is what a spectator or a recording gets.
    std::uint32_t OwnerPeer = 0;
    std::uint64_t Tick = 0;
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
    SnapshotApplyError Error = SnapshotApplyError::None;
    std::uint64_t Tick = 0;
    std::uint32_t EntitiesSpawned = 0;
    // Spawns that named a recipe this build does not have. Not an error -- an
    // authority may run content a client did not register -- but the entity is
    // bare, so it is counted rather than silently dropped.
    std::uint32_t RecipesMissing = 0;
    std::uint32_t EntitiesUpdated = 0;
    std::uint32_t EntitiesDestroyed = 0;
    // Set when a predicted entity's authoritative position disagreed with what
    // this machine simulated for that tick. The caller moves the pawn; a
    // snapshot applier has no business deciding how a correction is served.
    std::optional<ClientPrediction::Correction> Prediction;

    [[nodiscard]] bool Ok() const { return Error == SnapshotApplyError::None; }
};

// Decodes a snapshot onto the target world. Structural work -- creating an
// entity for an identity not seen before, destroying one the snapshot says is
// gone -- happens here, so this must be called where structural mutation is
// legal: outside any active query, at the start of the client's tick.
[[nodiscard]] SnapshotApplyResult ReplicationApplySnapshot(
    const SnapshotApplyRequest& request,
    std::span<const std::byte> bytes);
