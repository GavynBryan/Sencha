#pragma once

#include <app/GameContexts.h>
#include <net/ClientPrediction.h>
#include <net/NetPlayerCommand.h>
#include <net/PawnCommandRing.h>
#include <net/ReplicationInterpolation.h>
#include <net/NetTickEstimator.h>
#include <net/NetSession.h>

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

class EngineSchedule;
class World;

//=============================================================================
// PeerCommandRuntime
//
// The session-facing half of the input channel, and the mirror of
// ReplicationRuntime: state travels out to peers, requests come back.
//
// It owns what has to persist between frames -- one arrival buffer per peer --
// and it is where a peer's request stops being a message and becomes something
// the simulation reads. That handoff is the whole point: once a command has
// been fed, the authority's own systems steer a remote player's pawn using the
// same code that steers the local one, differing only in which action source
// they read. Nothing downstream asks whether a player is here or elsewhere.
//
// Which pawn a peer drives is the authority's record -- the NetOwner it wrote
// when it spawned that pawn -- and never anything the message claims, so a peer
// cannot address another player's entity.
//=============================================================================
class PeerCommandRuntime
{
public:
    // Authority side. Takes one delivered payload, kind byte included. False
    // means it was not a command or would not decode; the session's strike
    // machinery decides what a peer that keeps sending those has earned.
    [[nodiscard]] bool Receive(PeerId peer, std::span<const std::byte> payload);

    // Authority side, once per fixed tick. Hands each peer's next record to its
    // input source and its aim to the pawn it owns.
    void Feed(World& world, std::uint64_t tick);

    // Client side, once per frame. Projects the newest ticks this machine has
    // simulated and not had answered onto the wire. Returns the bytes queued,
    // zero when there is nothing to send.
    //
    // Everything travels as it was captured. The records are already stamped on
    // the authority's clock and already carry the aim each tick ran with, so
    // nothing here re-samples anything: a value read at send time belongs to a
    // different moment than the tick it would be attributed to.
    std::size_t SendLocal(NetSession& session, const PawnCommandRing& ring);

    // How many ticks of input each peer's buffer holds back before consuming.
    // Applied at the next feed, so raising it mid-session costs latency
    // immediately and lowering it is paid off by the catch-up collapse.
    void SetTargetDepth(std::size_t ticks) { Target = ticks; }
    [[nodiscard]] std::size_t TargetDepth() const { return Target; }

    void ForgetPeer(PeerId peer);
    void Reset();

    // The newest command tick this peer has been credited with, for the
    // snapshot that tells it so. Zero for a peer that has not spoken yet, which
    // is also what "nothing of yours has been simulated" means.
    [[nodiscard]] std::uint64_t AckFor(PeerId peer) const;

    [[nodiscard]] std::size_t TrackedPeers() const { return Buffers.size(); }
    [[nodiscard]] const NetPeerCommandBuffer* Peer(PeerId peer) const;

private:
    std::unordered_map<PeerId, NetPeerCommandBuffer> Buffers;
    // What each peer's most recent fed tick consumed, so the aim that framed a
    // record reaches the pawn with that record. Rebuilt every feed.
    std::unordered_map<PeerId, NetCommandRecord> Consumed;
    std::size_t Target = NetPeerCommandBuffer::kTargetDepth;
    // Reused across frames, sized once to the largest datagram a channel will
    // carry, so sending a command allocates nothing.
    std::vector<std::byte> Scratch;
};

//=============================================================================
// PeerCommandFeedSystem
//
// Runs the feed on the tick clock rather than the frame clock. A frame that
// runs three ticks owes three ticks of a remote player's input, the same way it
// owes three ticks of the local player's; feeding once per frame would collapse
// them and quietly make remote players slower than local ones under load.
//
// Ordered before anything that reads actions.
//=============================================================================
class PeerCommandFeedSystem
{
public:
    explicit PeerCommandFeedSystem(PeerCommandRuntime& commands)
        : Commands(&commands)
    {
    }

    void FixedLogic(FixedLogicContext& ctx);

private:
    PeerCommandRuntime* Commands = nullptr;
};

// Registers the systems the net module contributes to a game's schedule. A game
// that never hosts or joins still registers them: they are inert with no
// session, and composing a schedule by role would make the schedule depend on
// something that is not known until a console command runs.
void RegisterNetSystems(EngineSchedule& schedule, PeerCommandRuntime& commands,
                        ClientPrediction& prediction,
                        ReplicationInterpolation& interpolation,
                        const NetTickEstimator& clock);
