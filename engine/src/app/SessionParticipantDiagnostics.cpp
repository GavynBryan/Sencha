#include <app/SessionParticipantDiagnostics.h>

#include <ecs/EntityText.h>
#include <ecs/World.h>
#include <net/NetParticipantIdentity.h>
#include <net/NetReplicationComponents.h>
#include <participant/ParticipantControl.h>
#include <participant/ParticipantDiagnostics.h>

#include <string_view>
#include <utility>

namespace
{
    struct ProjectedControl
    {
        EntityId Participant;
        EntityId Subject;
        std::uint32_t Peer = 0;
    };

    std::string_view Name(SessionParticipantInvariantIssue issue)
    {
        switch (issue)
        {
        case SessionParticipantInvariantIssue::MissingIdentity:
            return "participant has no network identity";
        case SessionParticipantInvariantIssue::ParticipantNotReplicated:
            return "participant is not replicated";
        case SessionParticipantInvariantIssue::DuplicatePeerIdentity:
            return "peer has multiple participants";
        case SessionParticipantInvariantIssue::BodyNotReplicated:
            return "participant body is not replicated";
        case SessionParticipantInvariantIssue::BodyOwnerMismatch:
            return "participant body owner disagrees with identity";
        case SessionParticipantInvariantIssue::DrivenSubjectMismatch:
            return "control subject disagrees with NetDrivenBy";
        case SessionParticipantInvariantIssue::OrphanDrivenSubject:
            return "NetDrivenBy has no matching participant control";
        }
        return "unknown session participant invariant";
    }

    // Null means the participant has no session half at all, so every later
    // invariant here would be comparing against nothing.
    [[nodiscard]] const NetParticipantIdentity* ResolveParticipantIdentity(
        const World& world, EntityId participant, SessionParticipantValidation& report)
    {
        const NetParticipantIdentity* identity =
            world.TryGet<NetParticipantIdentity>(participant);
        if (identity != nullptr)
            return identity;

        report.Failures.push_back({
            .Issue = SessionParticipantInvariantIssue::MissingIdentity,
            .Participant = participant,
            .Entity = {},
            .Peer = 0,
        });
        return nullptr;
    }

    // The authority peer is exempt: it legitimately backs the local participant
    // alongside every simulated one, so only remote peers are one-to-one.
    void ValidatePeerUniqueness(
        EntityId participant, std::uint32_t peer,
        std::vector<std::pair<std::uint32_t, EntityId>>& peerParticipants,
        SessionParticipantValidation& report)
    {
        if (peer == kNetAuthorityPeer)
            return;

        for (const auto& [otherPeer, otherParticipant] : peerParticipants)
        {
            if (otherPeer == peer)
            {
                report.Failures.push_back({
                    .Issue = SessionParticipantInvariantIssue::DuplicatePeerIdentity,
                    .Participant = participant,
                    .Entity = otherParticipant,
                    .Peer = peer,
                });
                break;
            }
        }
        peerParticipants.emplace_back(peer, participant);
    }

    void ValidateBodyOwnership(const World& world, EntityId participant,
                               const ParticipantControl& control, std::uint32_t peer,
                               SessionParticipantValidation& report)
    {
        if (!control.Body.IsValid() || !world.IsAlive(control.Body))
            return;

        if (!world.HasComponent<NetReplicated>(control.Body))
        {
            report.Failures.push_back({
                .Issue = SessionParticipantInvariantIssue::BodyNotReplicated,
                .Participant = participant,
                .Entity = control.Body,
                .Peer = peer,
            });
        }
        if (peer == kNetAuthorityPeer)
            return;

        const NetOwner* owner = world.TryGet<NetOwner>(control.Body);
        if (owner == nullptr || owner->Peer != peer)
        {
            report.Failures.push_back({
                .Issue = SessionParticipantInvariantIssue::BodyOwnerMismatch,
                .Participant = participant,
                .Entity = control.Body,
                .Peer = peer,
            });
        }
    }

    void ValidateDrivenSubject(const World& world, EntityId participant,
                               const ParticipantControl& control, std::uint32_t peer,
                               SessionParticipantValidation& report)
    {
        if (peer == kNetAuthorityPeer || !control.ControlSubject.IsValid()
            || !world.IsAlive(control.ControlSubject))
        {
            return;
        }

        const NetDrivenBy* driven = world.TryGet<NetDrivenBy>(control.ControlSubject);
        if (driven == nullptr || driven->Peer != peer)
        {
            report.Failures.push_back({
                .Issue = SessionParticipantInvariantIssue::DrivenSubjectMismatch,
                .Participant = participant,
                .Entity = control.ControlSubject,
                .Peer = peer,
            });
        }
    }

    // The reverse of DrivenSubjectMismatch: every replicated driver has to be
    // claimed by a control this walk saw.
    //
    // A client receives NetDrivenBy but not authority-only ParticipantControl.
    // With no projected controls there is no local half of the invariant to
    // compare, so this authority-side validator has nothing to audit.
    void ValidateOrphanDrivenSubjects(const World& world,
                                      const std::vector<ProjectedControl>& controls,
                                      SessionParticipantValidation& report)
    {
        if (controls.empty() || !world.IsRegistered<NetDrivenBy>())
            return;

        world.ForEachComponent<NetDrivenBy>(
            [&](EntityId entity, const NetDrivenBy& driven) {
                bool matched = false;
                for (const ProjectedControl& control : controls)
                {
                    if (control.Peer == driven.Peer && control.Subject == entity)
                    {
                        matched = true;
                        break;
                    }
                }
                if (!matched)
                {
                    report.Failures.push_back({
                        .Issue = SessionParticipantInvariantIssue::OrphanDrivenSubject,
                        .Participant = {},
                        .Entity = entity,
                        .Peer = driven.Peer,
                    });
                }
            });
    }
}

SessionParticipantValidation ValidateSessionParticipants(const World& world)
{
    SessionParticipantValidation report;
    if (!world.IsRegistered<ParticipantControl>())
        return report;

    std::vector<std::pair<std::uint32_t, EntityId>> peerParticipants;
    std::vector<ProjectedControl> controls;
    world.ForEachComponent<ParticipantControl>(
        [&](EntityId participant, const ParticipantControl& control) {
            const NetParticipantIdentity* identity =
                ResolveParticipantIdentity(world, participant, report);
            if (identity == nullptr)
                return;

            const std::uint32_t peer = identity->Peer;
            controls.push_back(ProjectedControl{
                .Participant = participant,
                .Subject = control.ControlSubject,
                .Peer = peer,
            });
            if (!world.HasComponent<NetReplicated>(participant))
            {
                report.Failures.push_back({
                    .Issue = SessionParticipantInvariantIssue::ParticipantNotReplicated,
                    .Participant = participant,
                    .Entity = participant,
                    .Peer = peer,
                });
            }

            ValidatePeerUniqueness(participant, peer, peerParticipants, report);
            ValidateBodyOwnership(world, participant, control, peer, report);
            ValidateDrivenSubject(world, participant, control, peer, report);
        });

    ValidateOrphanDrivenSubjects(world, controls, report);
    return report;
}

std::string FormatSessionParticipantStatus(const World& world)
{
    std::string out = FormatParticipantStatus(world);
    std::size_t identityCount = 0;
    if (world.IsRegistered<NetParticipantIdentity>())
    {
        world.ForEachComponent<NetParticipantIdentity>(
            [&](EntityId participant, const NetParticipantIdentity& identity) {
                ++identityCount;
                out += "\nnetwork participant ";
                AppendEntityText(out, participant);
                out += " peer ";
                out += std::to_string(identity.Peer);
            });
    }
    if (identityCount == 0)
        out += "\nno network participant identities";

    const SessionParticipantValidation validation =
        ValidateSessionParticipants(world);
    out += validation.Ok() ? "\nsession projection ok"
                           : "\nsession projection FAILED";
    for (const SessionParticipantInvariantFailure& failure : validation.Failures)
    {
        out += "\n  ";
        out += Name(failure.Issue);
        out += " peer ";
        out += std::to_string(failure.Peer);
        out += " participant ";
        AppendEntityText(out, failure.Participant);
        out += " entity ";
        AppendEntityText(out, failure.Entity);
    }
    return out;
}
