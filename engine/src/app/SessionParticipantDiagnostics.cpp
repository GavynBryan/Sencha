#include <app/SessionParticipantDiagnostics.h>

#include <ecs/World.h>
#include <net/NetParticipantIdentity.h>
#include <net/NetReplicationComponents.h>
#include <participant/ParticipantControl.h>
#include <participant/ParticipantDiagnostics.h>

#include <cstdio>
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

    void AppendEntity(std::string& out, EntityId entity)
    {
        char buffer[48];
        const int length = std::snprintf(buffer, sizeof(buffer), "%u:%u",
                                         entity.Index, entity.Generation);
        if (length > 0)
            out.append(buffer, static_cast<std::size_t>(length));
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
                world.TryGet<NetParticipantIdentity>(participant);
            if (identity == nullptr)
            {
                report.Failures.push_back({
                    .Issue = SessionParticipantInvariantIssue::MissingIdentity,
                    .Participant = participant,
                    .Entity = {},
                    .Peer = 0,
                });
                return;
            }

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

            if (peer != kNetAuthorityPeer)
            {
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

            if (control.Body.IsValid() && world.IsAlive(control.Body))
            {
                if (!world.HasComponent<NetReplicated>(control.Body))
                {
                    report.Failures.push_back({
                        .Issue = SessionParticipantInvariantIssue::BodyNotReplicated,
                        .Participant = participant,
                        .Entity = control.Body,
                        .Peer = peer,
                    });
                }
                if (peer != kNetAuthorityPeer)
                {
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
            }

            if (peer != kNetAuthorityPeer && control.ControlSubject.IsValid()
                && world.IsAlive(control.ControlSubject))
            {
                const NetDrivenBy* driven =
                    world.TryGet<NetDrivenBy>(control.ControlSubject);
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
        });

    // A client receives NetDrivenBy but not authority-only ParticipantControl.
    // With no projected controls there is no local half of the invariant to
    // compare, so this authority-side validator has nothing to audit.
    if (!controls.empty() && world.IsRegistered<NetDrivenBy>())
    {
        world.ForEachComponent<NetDrivenBy>(
            [&](EntityId entity, const NetDrivenBy& driven) {
                bool matched = false;
                for (const ProjectedControl& control : controls)
                {
                    if (control.Peer == driven.Peer
                        && control.Subject == entity)
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
                AppendEntity(out, participant);
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
        AppendEntity(out, failure.Participant);
        out += " entity ";
        AppendEntity(out, failure.Entity);
    }
    return out;
}
