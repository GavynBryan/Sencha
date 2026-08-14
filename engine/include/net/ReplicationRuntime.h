#pragma once

#include <net/NetSession.h>
#include <net/NetSpawnRecipe.h>
#include <net/PeerCommandRuntime.h>
#include <net/NetDesyncProbe.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationSnapshot.h>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

class RuntimeWorld;
class World;
class WorldComponentSchema;

// One peer's interest set: the zones it should be holding open. Computed by
// whatever owns streaming policy; replication only ever compares it against
// what that peer already has.
//
// This is the whole relevance surface. A policy is whatever produces the list,
// and adding one never means touching a payload kind, an encoder, a channel, or
// the snapshot writer:
//
//   - who is near what      NetZoneStreaming, from what each peer drives
//   - how far that reaches  HopCount and Radius, per zone, through the graph's
//                           streaming overrides -- already data
//   - always relevant       add the zone to every peer's list
//   - same-zone only        pass the focus zone alone
//   - a rule of your own    build your own spans and pass those instead
//
// `Zones` must be sorted ascending and free of duplicates. The reconcile
// binary-searches it, and an unsorted list does not fail -- it revokes rooms the
// peer wanted and regrants them next frame, which reads as streaming thrash
// rather than as a caller mistake. Asserted where it is read.
//
// See test/runtime/ZoneRelevancePolicyTests.cpp, which holds this claim to
// three policies written entirely outside net/.
struct NetPeerZoneInterest
{
    PeerId Peer;
    std::span<const ZoneId> Zones;
};

// Ticks between snapshots out of the box: 30Hz against a 60Hz simulation. The
// rate a snapshot is worth sending at is set by how fast a player can perceive
// a change, not by how fast the world is stepped, and the interpolation buffer
// covers the difference. Halving the rate halves the authority's outbound bill
// per peer, which is the term that decides how many players fit.
inline constexpr std::uint32_t kNetDefaultSnapshotInterval = 2;

// The most one snapshot can be. A snapshot rides the unreliable channel, so it
// has to fit a single datagram: there is no fragmentation to fall back on, and
// no point resending, because the next snapshot supersedes this one before a
// resend could arrive. Anything that does not fit is deferred to the next.
inline constexpr std::size_t kNetMaxSnapshotBytes =
    kNetMaxPayloadBytes - kNetPayloadKindBytes;

// Smallest budget worth offering. Below roughly this a snapshot is envelope and
// little else, and entities start failing to fit one at a time.
inline constexpr std::size_t kNetMinSnapshotBytes = 128;

//=============================================================================
// ReplicationRuntime
//
// The session-facing half of replication: it owns what has to persist between
// frames -- the authority's identity mint, one baseline per connected peer, and
// a client's map of what it has been told about -- and turns that into
// snapshots leaving and snapshots arriving.
//
// Everything below it is pure. This is where per-peer state lives, so it is
// also where a peer joining or leaving is handled: a new peer starts with no
// baseline and therefore gets full state, and a departed one's baseline is
// released rather than kept against a peer that will never ack again.
//=============================================================================
class ReplicationRuntime
{
public:
    struct PublishStats
    {
        std::uint32_t PeersServed = 0;
        std::uint32_t SnapshotsSent = 0;
        std::size_t BytesQueued = 0;
        // Summed over the peers served. Deferred entities are the budget doing
        // its job and are only worth watching as a trend; unsendable ones are
        // never worth anything but a fix, since nothing about them can reach the
        // peer at all.
        std::uint32_t EntitiesDeferred = 0;
        std::uint32_t EntitiesUnsendable = 0;
        // The longest any one entity has waited to be carried to a peer that is
        // owed it, in snapshots, across all peers served. This is the number
        // that says whether deferral is a busy moment or a queue that is not
        // draining.
        std::uint32_t OldestDeferredSnapshots = 0;
        // The fullest snapshot written, against the budget it was written to.
        // Occupancy well under the budget with entities still deferred means
        // something other than bytes is doing the limiting.
        std::size_t PeakSnapshotBytes = 0;
        std::size_t BudgetBytes = 0;
        // Destroys that did not fit. Owed until confirmed either way, but a
        // number that stays up means a sweep is draining slower than it grows.
        std::uint32_t DestroysDeferred = 0;
        // What the bytes bought, summed over the peers served: entities a peer
        // had confirmed nothing about, against differences from what it holds.
        // A body that is still mostly seeding seconds after a join is a peer
        // that is not converging, which used to look like plain bandwidth.
        std::size_t SeedingBytes = 0;
        std::size_t DeltaBytes = 0;
        // The costliest few entities of this publish, largest first, taking the
        // most any one peer paid for each. Bounded and deliberately small: it
        // answers "which entity is eating the budget" without a capture format,
        // a file, or a viewer.
        std::array<SnapshotWriteResult::EntityCost,
                   SnapshotWriteResult::kCostliestTracked> Costliest{};
    };

    // Authority side. Writes one snapshot per connected peer and queues it on
    // the unreliable channel: a snapshot that arrives late is worthless, since
    // the next one supersedes it, so there is nothing to gain from resending.
    // `commands` supplies each peer's acknowledgement -- how far the authority
    // has got through that peer's input -- so a snapshot tells a client both
    // what the world is and which of the client's own guesses it accounts for.
    // Null acknowledges nothing, which is right for a recording.
    //
    // Called every frame; whether it publishes is its own business (see
    // SetPublishInterval). Peer bookkeeping happens on every call regardless of
    // cadence, so a peer that leaves stops costing memory at once.
    //
    // `zones` is what tells the change store which zone each entity is resident
    // in. Null on a world with no partition runtime, where every entity is
    // persistent.
    PublishStats Publish(NetSession& session, World& world,
                         const ReplicationLayout& layout, std::uint64_t tick,
                         const PeerCommandRuntime* commands = nullptr,
                         const RuntimeWorld* zones = nullptr);

    //-------------------------------------------------------------------------
    // Zone scope
    //
    // Which rooms each peer is holding open. Kept here because it is per-peer
    // replication state and this is where per-peer replication state lives; the
    // policy that decides what a peer is interested in is somebody else's, and
    // arrives as a list of zones.
    //-------------------------------------------------------------------------

    struct ZoneScopeStats
    {
        std::uint32_t Grants = 0;
        std::uint32_t Revokes = 0;
        std::size_t BytesQueued = 0;
    };

    // Brings every connected peer's scope into agreement with its interest set,
    // sending only the differences.
    //
    // A peer with no entry in `interest` is interested in nothing and has
    // everything revoked. Default deny rather than default keep: a caller that
    // forgot a peer leaves it holding rooms nobody is near, and the failure of
    // the opposite default -- a peer quietly retaining the whole world -- is
    // exactly the one this mechanism exists to prevent.
    //
    // Each interest list must be sorted ascending and free of duplicates, which
    // is what a zone demand set already is.
    ZoneScopeStats PublishZoneScope(NetSession& session,
                                    std::span<const NetPeerZoneInterest> interest);

    // Which zones scope control applies to: everything the streaming manifest
    // names. Replaces whatever was set; empty means no zone is gated.
    //
    // Necessary because residency and streaming are not the same thing. A map
    // loaded whole is one resident zone that no policy names, so nothing would
    // ever compute interest for it and nothing would ever grant it -- and a
    // snapshot writer that gated it would withhold the entire level from every
    // peer forever. Declared rather than inferred, because "nobody has granted
    // this yet" and "nobody ever will" look identical from inside the writer.
    void SetStreamedZones(std::span<const ZoneId> zones);
    [[nodiscard]] std::span<const ZoneId> StreamedZones() const
    {
        return StreamedZones_;
    }

    // A peer reports it has finished loading a zone. False when it names one it
    // was never granted, which is a peer claiming a room nobody offered it.
    [[nodiscard]] bool AcknowledgeZone(PeerId peer, ZoneId zone);

    //-------------------------------------------------------------------------
    // Desync probes
    //
    // Dev-only, off unless asked for. See NetDesyncProbe.h for what is folded
    // and why the two rules that shape it are not optional.
    //-------------------------------------------------------------------------

    // Ticks between probes; zero is off.
    void SetDesyncInterval(std::uint32_t ticks) { DesyncInterval = ticks; }
    [[nodiscard]] std::uint32_t DesyncIntervalTicks() const
    {
        return DesyncInterval;
    }

    struct DesyncStats
    {
        std::uint32_t Reports = 0;
        std::size_t BytesQueued = 0;
    };

    // Authority: one report per peer when the cadence has come round. Called
    // after Publish, so what it describes is what the peer was just told.
    DesyncStats PublishDesync(NetSession& session, const ReplicationLayout& layout,
                              std::uint64_t tick);

    // Client: compares an arriving report against this machine's own state.
    [[nodiscard]] NetDesyncResult CheckDesync(
        std::span<const std::byte> payload, const World& world,
        const ReplicationLayout& layout,
        const ReplicationInterpolation* interpolation, PeerId self,
        std::uint64_t* reportedTick = nullptr);

    // Client side: one grant or revoke from the authority, recorded.
    void ApplyZoneScope(const NetZoneScopeUpdate& update);

    // What the authority has told this machine to hold. Granted means it has
    // been asked for and not yet confirmed; acked means the confirmation has
    // gone out and state for it may arrive.
    [[nodiscard]] const NetZoneScope& LocalZones() const { return LocalScope; }

    // Client side: confirms every granted zone this machine has actually
    // finished loading, and says nothing about the rest.
    //
    // Asked of the world rather than answered by whoever started the load,
    // because "attached and finalized" is the world's own fact and the
    // confirmation has to mean exactly that. A zone acked while it is still
    // importing is a room the authority begins filling before there is anywhere
    // to put it.
    //
    struct ZoneAckStats
    {
        std::uint32_t Acks = 0;
        std::size_t BytesQueued = 0;
    };

    ZoneAckStats AcknowledgeResidentZones(NetSession& session,
                                          const RuntimeWorld& world);

    // Simulation ticks between snapshots. One publishes as fast as the world
    // moves; higher trades freshness for bandwidth, which is the trade that
    // decides how many players fit in a session -- the cost of a snapshot is
    // paid per peer, so halving the rate halves the authority's whole outbound
    // bill. Mirrored motion stays smooth because the interpolation buffer
    // already presents between samples rather than stepping on arrival.
    //
    // Zero and one both mean every tick.
    void SetPublishInterval(std::uint32_t ticks) { PublishInterval = ticks; }
    [[nodiscard]] std::uint32_t PublishIntervalTicks() const
    {
        return PublishInterval;
    }

    // Bytes one snapshot may occupy, clamped to what a datagram can carry.
    // Lowering it does not lose anything: what does not fit is deferred and
    // arrives in a later snapshot, so this trades how quickly a peer converges
    // on the world against how much of the link the authority takes to do it.
    void SetSnapshotBytes(std::size_t bytes);
    [[nodiscard]] std::size_t SnapshotBytes() const { return Budget; }

    // What the last publish that actually went out cost. Held rather than
    // returned only, because the frame that publishes and the panel that reads
    // are different callers, and a publish the cadence skipped has nothing to
    // say -- reporting zeroes for it would read as a session that stopped.
    [[nodiscard]] const PublishStats& LastPublish() const { return Published; }

    // Client side. `payload` is one channel message, still carrying its kind
    // byte. Returns what happened; a payload that is not a snapshot is ignored
    // rather than treated as an error, because other kinds share this channel.
    SnapshotApplyResult Apply(std::span<const std::byte> payload, World& world,
                              const WorldComponentSchema& schema,
                              const ReplicationLayout& layout,
                              const NetSpawnRecipes* recipes = nullptr,
                              ClientPrediction* prediction = nullptr,
                              ReplicationInterpolation* interpolation = nullptr);

    // Client side. What the last snapshot this machine applied did to it:
    // spawned, updated, destroyed, components removed, authored entities bound
    // or deferred, recipes it had no builder for. Every one of these was
    // computed and discarded, so "why did that not appear" had no answer short
    // of a debugger.
    [[nodiscard]] const SnapshotApplyResult& LastApply() const { return Applied; }

    // Client side. The newest snapshot tick this machine has applied. Used for
    // the shared clock; what the authority is told about delivery is the ack
    // below, which names snapshots rather than moments.
    [[nodiscard]] std::uint64_t AppliedSnapshot() const { return AppliedTick; }

    // Client side. Which snapshots this machine has actually applied, which is
    // what travels back so the next difference is measured from a state it
    // really reached.
    [[nodiscard]] const NetSnapshotAck& AppliedAck() const { return AppliedAcks; }

    // A peer that left keeps no baseline: it would be a growing memory cost
    // against a peer that will never receive anything again, and a peer id can
    // be reused.
    void ForgetPeer(PeerId peer);
    // Session over. Identity and baselines are session-transient by definition.
    void Reset();

    [[nodiscard]] std::size_t TrackedPeers() const { return Peers.size(); }
    [[nodiscard]] const ReplicationClientIdentity& ClientEntities() const
    {
        return ClientMap;
    }

    // The authority's half of the same question: which of this machine's
    // entities a wire identity names. Const on purpose -- minting belongs to
    // the publish walk, and an identity handed out for an entity the walk never
    // visits is one nothing would ever release.
    [[nodiscard]] const ReplicationAuthorityIdentity& AuthorityEntities() const
    {
        return Identity;
    }

    // What has been published and when each part of it last moved, and how far
    // through that history one peer has been carried. Together these answer why
    // a field has or has not reached a peer, which was previously answerable
    // only by reading the writer.
    //
    // Const, and const is load-bearing. Both are the publish walk's to write:
    // a floor moved by anything else is a peer credited with state it never
    // proved it holds, which is silent and permanent.
    [[nodiscard]] const ReplicationChangeStore& PublishedState() const
    {
        return Changes;
    }
    // Null for a peer this authority is not serving.
    [[nodiscard]] const ReplicationPeerState* PeerBaseline(PeerId peer) const;

private:
    ReplicationAuthorityIdentity Identity;
    ReplicationChangeStore Changes;
    // Counts publishes, and is what "since" means in every floor and every
    // change stamp. Its own counter rather than the simulation tick, because it
    // must increase on every pass and a tick need not.
    std::uint64_t Generation = 0;
    std::unordered_map<PeerId, ReplicationPeerState> Peers;
    // A client has one authority, so it has one scope rather than a map.
    NetZoneScope LocalScope;
    // Ascending: the writer binary-searches it once per entity per peer.
    std::vector<ZoneId> StreamedZones_;
    ReplicationClientIdentity ClientMap;
    SnapshotApplyResult Applied;
    std::uint64_t AppliedTick = 0;
    NetSnapshotAck AppliedAcks;

    // The shipping cadence, not one-per-tick: a cvar's OnChange fires on
    // change, so a default that disagreed with the cvar's would mean the
    // engine ran at a rate nobody chose until someone happened to set it.
    std::uint32_t PublishInterval = kNetDefaultSnapshotInterval;
    std::uint32_t DesyncInterval = 0;
    std::uint64_t LastDesyncTick = 0;
    bool HasProbed = false;
    std::vector<NetDesyncSample> ProbeSamples;
    // The tick a snapshot last went out on, and whether one ever has. Compared
    // as a difference rather than a remainder on purpose: a host running slower
    // than its tick rate advances the tick index by several per frame, and a
    // remainder test on a stride it never lands on would stop publishing for
    // the rest of the session.
    std::uint64_t LastPublishedTick = 0;
    bool HasPublished = false;
    std::size_t Budget = kNetMaxSnapshotBytes;
    PublishStats Published;
    // Reused across peers and frames. Sized once to the largest datagram a
    // channel will fragment for us, so publishing allocates nothing per frame.
    std::vector<std::byte> Scratch;
};
