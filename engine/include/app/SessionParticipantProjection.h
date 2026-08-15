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

    SessionParticipantAdmission AdmitLocal(World& world);
    // `source` belongs to the input producer (bot, replay, script); it is not
    // fabricated from a peer id.
    SessionParticipantAdmission AdmitSimulated(
        World& world, InputActionSourceId source);
    // Idempotent per valid peer. Admission, body assignment, identity,
    // replication, ownership, driven-subject, and source allocation settle
    // before this returns.
    SessionParticipantAdmission AdmitPeer(World& world, PeerId peer);

    ParticipantBodyChange RequestBody(World& world, EntityId participant);
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
        InputActionSourceId source);
    void ProjectControl(World& world, const ParticipantControlChange& change,
                        std::uint32_t peer);

    ParticipantLifecycle Lifecycle;
};
