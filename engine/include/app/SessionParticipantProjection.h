#pragma once

#include <net/NetSession.h>
#include <participant/ParticipantLifecycle.h>

class ClientPrediction;
class World;

struct SessionParticipantAdmission
{
    ParticipantAdmission Admission;
    ParticipantBodyChange Body;
};

enum class SessionParticipantRetirementStatus : std::uint8_t
{
    Retired,
    NotFound,
    PeerBound,
};

struct SessionParticipantRetirement
{
    SessionParticipantRetirementStatus Status =
        SessionParticipantRetirementStatus::NotFound;
    ParticipantRetirement Retirement;
};

// The concrete owner of the compound session invariant. ParticipantLifecycle
// changes generic participant/control state; this type consumes the outcome in
// the same call and projects peer identity, replication, ownership, driven
// subject, prediction, and peer-source lifetime.
class SessionParticipantProjection
{
public:
    [[nodiscard]] ParticipantPolicies& Policies()
    {
        return Lifecycle.Policies();
    }
    [[nodiscard]] const ParticipantPolicies& Policies() const
    {
        return Lifecycle.Policies();
    }

    // `sessionActive` is whether this process is in a net session right now.
    // Replication state -- the peer identity and the replicated marker -- is
    // added only when it is, because a singleplayer game's participant and
    // body have no business in the replicated table. A session that starts
    // later stamps them retroactively through ProjectSessionStart.
    SessionParticipantAdmission AdmitLocal(World& world, bool sessionActive);
    // `source` belongs to the input producer (bot, replay, script); it is not
    // fabricated from a peer id.
    SessionParticipantAdmission AdmitSimulated(
        World& world, InputActionSourceId source, bool sessionActive);
    // Idempotent per valid peer. Admission, body assignment, identity,
    // replication, ownership, driven-subject, and source allocation settle
    // before this returns.
    SessionParticipantAdmission AdmitPeer(World& world, PeerId peer);

    ParticipantBodyChange RequestBody(World& world, EntityId participant,
                                      bool sessionActive);

    // The retroactive half of `sessionActive`: every participant admitted before
    // a session existed, and every body it holds, gets the replication state a
    // session-time admission would have given it. Idempotent. Session end does
    // not undo it -- a body that was once replicated keeps its marker until it
    // is reaped, which is a smaller lie than a table that changes shape under a
    // live peer.
    void ProjectSessionStart(World& world);
    ParticipantControlChange SetControlSubject(
        World& world, EntityId participant, EntityId subject);
    SessionParticipantRetirement RetireParticipant(
        World& world, EntityId participant);
    // Peer-bound participants can only leave through this path. It also clears
    // everything the peer owned and closes its command source.
    SessionParticipantRetirement RetirePeer(World& world, PeerId peer);

    // Query the replicated NetDrivenBy fact, then apply local presentation and
    // prediction. Called after snapshot application, outside ECS queries.
    void ReconcileClientControl(World& world, PeerId self,
                                ClientPrediction& prediction);

private:
    SessionParticipantAdmission Admit(
        World& world, PeerId peer, ParticipantPresence presence,
        InputActionSourceId source, bool sessionActive);
    void ProjectControl(World& world, const ParticipantControlChange& change,
                        std::uint32_t peer);

    ParticipantLifecycle Lifecycle;
};
