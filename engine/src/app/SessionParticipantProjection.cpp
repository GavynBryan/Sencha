#include <app/SessionParticipantProjection.h>

#include <ecs/World.h>
#include <net/ClientPrediction.h>
#include <net/NetDrivenSubject.h>
#include <net/NetOwnership.h>
#include <net/NetParticipantIdentity.h>
#include <net/NetPeerInputSource.h>
#include <net/NetReplicationComponents.h>
#include <participant/LocalControl.h>

namespace
{
    std::uint32_t PeerOf(const World& world, EntityId participant)
    {
        const NetParticipantIdentity* identity =
            world.TryGet<NetParticipantIdentity>(participant);
        return identity == nullptr ? kNetAuthorityPeer : identity->Peer;
    }

    void AddReplicationMarker(World& world, EntityId entity)
    {
        if (world.IsAlive(entity) && world.IsRegistered<NetReplicated>()
            && !world.HasComponent<NetReplicated>(entity))
        {
            world.AddComponent<NetReplicated>(entity, {});
        }
    }

    void SetIdentity(World& world, EntityId participant, std::uint32_t peer)
    {
        if (!world.IsRegistered<NetParticipantIdentity>())
            return;
        if (NetParticipantIdentity* identity =
                world.TryGet<NetParticipantIdentity>(participant))
        {
            identity->Peer = peer;
        }
        else
        {
            world.AddComponent<NetParticipantIdentity>(
                participant, NetParticipantIdentity{ .Peer = peer });
        }
    }

    void RemoveDrivenBy(World& world, EntityId subject, std::uint32_t peer)
    {
        if (!subject.IsValid() || !world.IsAlive(subject)
            || !world.IsRegistered<NetDrivenBy>())
        {
            return;
        }
        const NetDrivenBy* driven = world.TryGet<NetDrivenBy>(subject);
        if (driven != nullptr && driven->Peer == peer)
            world.RemoveComponent<NetDrivenBy>(subject);
    }

    ParticipantBodyChange DescribeExistingBody(
        const World& world, EntityId participant)
    {
        ParticipantBodyChange result;
        result.Participant = participant;
        const ParticipantControl* control =
            world.TryGet<ParticipantControl>(participant);
        if (control != nullptr && control->Body.IsValid()
            && world.IsAlive(control->Body))
        {
            result.Status = ParticipantBodyStatus::AlreadyAssigned;
            result.Body = control->Body;
        }
        else
        {
            result.Status = ParticipantBodyStatus::Unavailable;
        }
        return result;
    }
}

SessionParticipantAdmission SessionParticipantProjection::AdmitLocal(World& world)
{
    return Admit(world, PeerId{}, ParticipantPresence::Local,
                 kLocalInputActionSource);
}

SessionParticipantAdmission SessionParticipantProjection::AdmitSimulated(
    World& world, InputActionSourceId source)
{
    return Admit(world, PeerId{}, ParticipantPresence::Simulated, source);
}

SessionParticipantAdmission SessionParticipantProjection::AdmitPeer(
    World& world, PeerId peer)
{
    if (!peer.IsValid() || !world.IsRegistered<ParticipantControl>())
        return {};

    if (const EntityId existing = NetParticipantForPeer(world, peer);
        existing.IsValid())
    {
        SessionParticipantAdmission result;
        result.Admission = ParticipantAdmission{
            .Status = ParticipantAdmissionStatus::Existing,
            .Participant = existing,
        };
        result.Body = DescribeExistingBody(world, existing);
        return result;
    }
    return Admit(world, peer, ParticipantPresence::Simulated,
                 NetSourceForPeer(world, peer));
}

SessionParticipantAdmission SessionParticipantProjection::Admit(
    World& world, PeerId peer, ParticipantPresence presence,
    InputActionSourceId source)
{
    SessionParticipantAdmission result;
    result.Admission = Lifecycle.Admit(world, source, presence);
    const EntityId participant = result.Admission.Participant;
    if (!participant.IsValid())
        return result;

    SetIdentity(world, participant,
                peer.IsValid() ? peer.Value : kNetAuthorityPeer);
    AddReplicationMarker(world, participant);

    // Re-admission is observation, not a lifecycle event. In particular it
    // neither recomposes game-owned state nor asks the game for a body again.
    // Respawn/content-ready callers use RequestBody explicitly.
    result.Body = result.Admission.Created()
        ? RequestBody(world, participant)
        : DescribeExistingBody(world, participant);
    return result;
}

ParticipantBodyChange SessionParticipantProjection::RequestBody(
    World& world, EntityId participant)
{
    ParticipantBodyChange result = Lifecycle.RequestBody(world, participant);
    if (result.Status != ParticipantBodyStatus::Assigned)
        return result;

    const std::uint32_t peer = PeerOf(world, participant);
    AddReplicationMarker(world, result.Body);
    if (peer != kNetAuthorityPeer)
        NetSetOwner(world, result.Body, PeerId{ peer });
    ProjectControl(world, result.Control, peer);
    return result;
}

ParticipantControlChange SessionParticipantProjection::SetControlSubject(
    World& world, EntityId participant, EntityId subject)
{
    const std::uint32_t peer = PeerOf(world, participant);
    ParticipantControlChange change =
        Lifecycle.SetControlSubject(world, participant, subject);
    ProjectControl(world, change, peer);
    return change;
}

void SessionParticipantProjection::ProjectControl(
    World& world, const ParticipantControlChange& change, std::uint32_t peer)
{
    if (!change.Changed())
        return;

    RemoveDrivenBy(world, change.PreviousSubject, peer);

    if (change.DisplacedParticipant.IsValid())
    {
        const std::uint32_t displacedPeer =
            PeerOf(world, change.DisplacedParticipant);
        RemoveDrivenBy(world, change.CurrentSubject, displacedPeer);
    }

    if (peer == kNetAuthorityPeer || !change.CurrentSubject.IsValid()
        || !world.IsRegistered<NetDrivenBy>())
    {
        return;
    }

    if (NetDrivenBy* driven = world.TryGet<NetDrivenBy>(change.CurrentSubject))
        driven->Peer = peer;
    else
        world.AddComponent<NetDrivenBy>(change.CurrentSubject,
                                        NetDrivenBy{ .Peer = peer });
}

SessionParticipantRetirement SessionParticipantProjection::RetireParticipant(
    World& world, EntityId participant)
{
    if (PeerOf(world, participant) != kNetAuthorityPeer)
    {
        return SessionParticipantRetirement{
            .Status = SessionParticipantRetirementStatus::PeerBound,
            .Retirement = ParticipantRetirement{
                .Status = ParticipantRetirementStatus::NotFound,
                .Participant = participant,
                .ReleasedSubject = {},
                .Body = {},
                .BodyReaped = false,
            },
        };
    }

    const ParticipantControl* control =
        world.TryGet<ParticipantControl>(participant);
    const EntityId subject = control == nullptr
        ? EntityId{}
        : control->ControlSubject;
    ParticipantRetirement result = Lifecycle.Retire(world, participant);
    RemoveDrivenBy(world, subject, kNetAuthorityPeer);
    return SessionParticipantRetirement{
        .Status = result.Status == ParticipantRetirementStatus::Retired
            ? SessionParticipantRetirementStatus::Retired
            : SessionParticipantRetirementStatus::NotFound,
        .Retirement = result,
    };
}

SessionParticipantRetirement SessionParticipantProjection::RetirePeer(
    World& world, PeerId peer)
{
    if (!peer.IsValid())
        return {};

    const EntityId participant = NetParticipantForPeer(world, peer);
    const ParticipantControl* control =
        world.TryGet<ParticipantControl>(participant);
    const EntityId subject = control == nullptr
        ? EntityId{}
        : control->ControlSubject;

    ParticipantRetirement result = Lifecycle.Retire(world, participant);
    RemoveDrivenBy(world, subject, peer.Value);
    NetForgetOwnerPeer(world, peer);
    NetReleasePeerSource(world, peer);
    return SessionParticipantRetirement{
        .Status = result.Status == ParticipantRetirementStatus::Retired
            ? SessionParticipantRetirementStatus::Retired
            : SessionParticipantRetirementStatus::NotFound,
        .Retirement = result,
    };
}

void SessionParticipantProjection::ReconcileClientControl(
    World& world, PeerId self, ClientPrediction& prediction)
{
    if (!self.IsValid())
        return;
    const EntityId subject = NetDrivenSubjectForPeer(world, self);
    const LocalControlChange change = SetLocalControlSubject(world, subject);
    if (change.Changed)
        prediction.SetPredicted(subject);
}
