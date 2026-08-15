#include <participant/ParticipantDiagnostics.h>

#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <participant/LocalControl.h>
#include <participant/ParticipantControl.h>

#include <cstdio>
#include <string_view>
#include <utility>

namespace
{
    std::string_view Name(ParticipantInvariantIssue issue)
    {
        switch (issue)
        {
        case ParticipantInvariantIssue::DuplicateLocalParticipant:
            return "duplicate local participant";
        case ParticipantInvariantIssue::DuplicateControlSubject:
            return "control subject has multiple participants";
        case ParticipantInvariantIssue::DeadControlSubject:
            return "control subject is dead";
        case ParticipantInvariantIssue::MissingInputReference:
            return "non-local source has no input reference";
        case ParticipantInvariantIssue::UnexpectedLocalInputReference:
            return "local source is redundantly stored as a reference";
        case ParticipantInvariantIssue::WrongInputSource:
            return "input reference names the wrong source";
        case ParticipantInvariantIssue::LocalControlMismatch:
            return "local participant and local-control resource disagree";
        }
        return "unknown participant invariant";
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

ParticipantValidation ValidateParticipants(const World& world)
{
    ParticipantValidation report;
    if (!world.IsRegistered<ParticipantControl>())
        return report;

    std::vector<std::pair<EntityId, EntityId>> subjects;
    EntityId localParticipant;
    bool sawParticipant = false;
    world.ForEachComponent<ParticipantControl>(
        [&](EntityId participant, const ParticipantControl& control) {
            sawParticipant = true;
            if (world.IsRegistered<LocalParticipant>()
                && world.HasComponent<LocalParticipant>(participant))
            {
                if (localParticipant.IsValid())
                {
                    report.Failures.push_back({
                        .Issue = ParticipantInvariantIssue::DuplicateLocalParticipant,
                        .Participant = participant,
                        .Subject = localParticipant,
                    });
                }
                else
                {
                    localParticipant = participant;
                }
            }

            if (!control.ControlSubject.IsValid())
                return;
            if (!world.IsAlive(control.ControlSubject))
            {
                report.Failures.push_back({
                    .Issue = ParticipantInvariantIssue::DeadControlSubject,
                    .Participant = participant,
                    .Subject = control.ControlSubject,
                });
                return;
            }

            for (const auto& [otherSubject, otherParticipant] : subjects)
            {
                if (otherSubject == control.ControlSubject)
                {
                    report.Failures.push_back({
                        .Issue = ParticipantInvariantIssue::DuplicateControlSubject,
                        .Participant = participant,
                        .Subject = otherParticipant,
                    });
                    break;
                }
            }
            subjects.emplace_back(control.ControlSubject, participant);

            const InputActionSourceRef* input =
                world.TryGet<InputActionSourceRef>(control.ControlSubject);
            if (control.Source == kLocalInputActionSource)
            {
                if (input != nullptr)
                {
                    report.Failures.push_back({
                        .Issue = ParticipantInvariantIssue::UnexpectedLocalInputReference,
                        .Participant = participant,
                        .Subject = control.ControlSubject,
                    });
                }
            }
            else if (input == nullptr)
            {
                report.Failures.push_back({
                    .Issue = ParticipantInvariantIssue::MissingInputReference,
                    .Participant = participant,
                    .Subject = control.ControlSubject,
                });
            }
            else if (input->Source != control.Source)
            {
                report.Failures.push_back({
                    .Issue = ParticipantInvariantIssue::WrongInputSource,
                    .Participant = participant,
                    .Subject = control.ControlSubject,
                });
            }
        });

    EntityId expectedLocal;
    if (localParticipant.IsValid())
    {
        if (const ParticipantControl* control =
                world.TryGet<ParticipantControl>(localParticipant))
        {
            expectedLocal = control->ControlSubject;
        }
    }
    if (sawParticipant && LocalControlSubjectOf(world) != expectedLocal)
    {
        report.Failures.push_back({
            .Issue = ParticipantInvariantIssue::LocalControlMismatch,
            .Participant = localParticipant,
            .Subject = LocalControlSubjectOf(world),
        });
    }

    return report;
}

std::string FormatParticipantStatus(const World& world)
{
    std::string out;
    std::size_t count = 0;
    if (world.IsRegistered<ParticipantControl>())
    {
        world.ForEachComponent<ParticipantControl>(
            [&](EntityId participant, const ParticipantControl& control) {
                ++count;
                out += "participant ";
                AppendEntity(out, participant);
                out += " body ";
                if (control.Body.IsValid())
                    AppendEntity(out, control.Body);
                else
                    out += "none";
                out += " controls ";
                if (control.ControlSubject.IsValid())
                    AppendEntity(out, control.ControlSubject);
                else
                    out += "none";
                out += '\n';
            });
    }

    if (count == 0)
        out = "no participants\n";

    const EntityId local = LocalControlSubjectOf(world);
    out += "local control ";
    if (local.IsValid())
        AppendEntity(out, local);
    else
        out += "none";

    const ParticipantValidation validation = ValidateParticipants(world);
    out += validation.Ok() ? "\nvalidation ok" : "\nvalidation FAILED";
    for (const ParticipantInvariantFailure& failure : validation.Failures)
    {
        out += "\n  ";
        out += Name(failure.Issue);
        out += " participant ";
        AppendEntity(out, failure.Participant);
        out += " subject ";
        AppendEntity(out, failure.Subject);
    }
    return out;
}
