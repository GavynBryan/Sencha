#include <participant/ParticipantDiagnostics.h>

#include <ecs/EntityText.h>
#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <participant/LocalControl.h>
#include <participant/ParticipantControl.h>

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

    // `localParticipant` carries the first one seen across the whole walk, so
    // the second and later ones are the failures rather than the first.
    void ValidateLocalParticipantUniqueness(const World& world, EntityId participant,
                                            EntityId& localParticipant,
                                            ParticipantValidation& report)
    {
        if (!world.IsRegistered<LocalParticipant>()
            || !world.HasComponent<LocalParticipant>(participant))
        {
            return;
        }

        if (!localParticipant.IsValid())
        {
            localParticipant = participant;
            return;
        }

        report.Failures.push_back({
            .Issue = ParticipantInvariantIssue::DuplicateLocalParticipant,
            .Participant = participant,
            .Subject = localParticipant,
        });
    }

    // False means the subject cannot carry the remaining invariants: there is
    // either nothing at the controls, or what is there is already dead. Both
    // cases stop the caller, and only the second is a failure.
    //
    // `subjects` accumulates across the walk, so a subject claimed twice is
    // reported against the participant that claimed it second.
    [[nodiscard]] bool ValidateControlSubjectIdentity(
        const World& world, EntityId participant, const ParticipantControl& control,
        std::vector<std::pair<EntityId, EntityId>>& subjects,
        ParticipantValidation& report)
    {
        if (!control.ControlSubject.IsValid())
            return false;
        if (!world.IsAlive(control.ControlSubject))
        {
            report.Failures.push_back({
                .Issue = ParticipantInvariantIssue::DeadControlSubject,
                .Participant = participant,
                .Subject = control.ControlSubject,
            });
            return false;
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
        return true;
    }

    // Three readings of one reference, mutually exclusive: source zero is
    // represented by the component's absence, so its presence is as wrong as a
    // non-local source's absence.
    void ValidateInputReference(const World& world, EntityId participant,
                                const ParticipantControl& control,
                                ParticipantValidation& report)
    {
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
    }

    // Runs after the walk because it audits the resource against whichever
    // participant the walk settled on as the local one. An invalid
    // `localParticipant` expects an empty resource.
    void ValidateLocalControlProjection(const World& world, EntityId localParticipant,
                                        ParticipantValidation& report)
    {
        EntityId expectedLocal;
        if (localParticipant.IsValid())
        {
            if (const ParticipantControl* control =
                    world.TryGet<ParticipantControl>(localParticipant))
            {
                expectedLocal = control->ControlSubject;
            }
        }

        const EntityId held = LocalControlSubjectOf(world);
        if (held == expectedLocal)
            return;

        report.Failures.push_back({
            .Issue = ParticipantInvariantIssue::LocalControlMismatch,
            .Participant = localParticipant,
            .Subject = held,
        });
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
            ValidateLocalParticipantUniqueness(world, participant, localParticipant,
                                               report);
            if (!ValidateControlSubjectIdentity(world, participant, control, subjects,
                                                report))
            {
                return;
            }
            ValidateInputReference(world, participant, control, report);
        });

    // A world with no participants at all has no local half to disagree with,
    // so a stale resource there is not this validator's finding.
    if (sawParticipant)
        ValidateLocalControlProjection(world, localParticipant, report);

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
                AppendEntityText(out, participant);
                out += " body ";
                if (control.Body.IsValid())
                    AppendEntityText(out, control.Body);
                else
                    out += "none";
                out += " controls ";
                if (control.ControlSubject.IsValid())
                    AppendEntityText(out, control.ControlSubject);
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
        AppendEntityText(out, local);
    else
        out += "none";

    const ParticipantValidation validation = ValidateParticipants(world);
    out += validation.Ok() ? "\nvalidation ok" : "\nvalidation FAILED";
    for (const ParticipantInvariantFailure& failure : validation.Failures)
    {
        out += "\n  ";
        out += Name(failure.Issue);
        out += " participant ";
        AppendEntityText(out, failure.Participant);
        out += " subject ";
        AppendEntityText(out, failure.Subject);
    }
    return out;
}
