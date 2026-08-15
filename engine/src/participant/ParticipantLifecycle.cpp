#include <participant/ParticipantLifecycle.h>

#include <ecs/World.h>
#include <participant/LocalControl.h>

#include <vector>

namespace
{
    bool TracksParticipants(const World& world)
    {
        return world.IsRegistered<ParticipantControl>();
    }

    void ReleaseInputReference(World& world, EntityId subject)
    {
        if (!subject.IsValid() || !world.IsAlive(subject))
            return;
        if (world.IsRegistered<InputActionSourceRef>()
            && world.HasComponent<InputActionSourceRef>(subject))
        {
            world.RemoveComponent<InputActionSourceRef>(subject);
        }
    }

    void ApplyInputReference(World& world, EntityId subject,
                             InputActionSourceId source)
    {
        if (!world.IsRegistered<InputActionSourceRef>())
            return;

        // Source zero is represented by absence. Keeping a zero-valued
        // reference is redundant and defeats the input layer's sparse shape.
        if (source == kLocalInputActionSource)
        {
            ReleaseInputReference(world, subject);
            return;
        }

        if (InputActionSourceRef* ref = world.TryGet<InputActionSourceRef>(subject))
            ref->Source = source;
        else
            world.AddComponent<InputActionSourceRef>(
                subject, InputActionSourceRef{ .Source = source });
    }

    // Takes the subject away from whoever else is holding it, so that no frame
    // exists in which two participants steer one entity. Returns the first one
    // displaced, which is the one the caller reports.
    //
    // Collected under a const view first, then mutated: iterating non-const
    // would both count as a write for change detection and mutate the storage
    // being walked. The two passes are the point, not an accident.
    EntityId ClearSubjectFromOtherParticipants(World& world, EntityId participant,
                                               EntityId subject)
    {
        std::vector<EntityId> displaced;
        const World& reading = world;
        reading.ForEachComponent<ParticipantControl>(
            [&](EntityId other, const ParticipantControl& held) {
                if (other != participant && held.ControlSubject == subject)
                    displaced.push_back(other);
            });

        EntityId first;
        for (const EntityId other : displaced)
        {
            if (ParticipantControl* held = world.TryGet<ParticipantControl>(other))
                held->ControlSubject = EntityId{};
            if (world.IsRegistered<LocalParticipant>()
                && world.HasComponent<LocalParticipant>(other))
            {
                SetLocalControlSubject(world, EntityId{});
            }
            if (!first.IsValid())
                first = other;
        }
        return first;
    }

    // False means the participant went out from under us. The control pointer
    // is re-read on both sides of ApplyInputReference because that call adds or
    // removes a component, and an archetype move relocates rows.
    [[nodiscard]] bool BindSubject(World& world, EntityId participant, EntityId subject)
    {
        ParticipantControl* control = world.TryGet<ParticipantControl>(participant);
        if (control == nullptr)
            return false;
        ApplyInputReference(world, subject, control->Source);

        control = world.TryGet<ParticipantControl>(participant);
        if (control == nullptr)
            return false;
        control->ControlSubject = subject;
        return true;
    }
}

EntityId LocalParticipantOf(const World& world)
{
    if (!TracksParticipants(world) || !world.IsRegistered<LocalParticipant>())
        return EntityId{};

    EntityId found;
    world.ForEachComponent<ParticipantControl>(
        [&](EntityId participant, const ParticipantControl&) {
            if (!found.IsValid()
                && world.HasComponent<LocalParticipant>(participant))
            {
                found = participant;
            }
        });
    return found;
}

ParticipantAdmission ParticipantLifecycle::Admit(
    World& world, InputActionSourceId source, ParticipantPresence presence,
    StoragePartitionId partition)
{
    if (!TracksParticipants(world))
        return {};

    if (presence == ParticipantPresence::Local)
    {
        if (const EntityId existing = LocalParticipantOf(world); existing.IsValid())
        {
            return ParticipantAdmission{
                .Status = ParticipantAdmissionStatus::Existing,
                .Participant = existing,
            };
        }
    }

    const EntityId participant = world.CreateEntity(partition);
    world.AddComponent<ParticipantControl>(
        participant, ParticipantControl{
            .Source = source,
            .Body = {},
            .ControlSubject = {},
        });
    if (presence == ParticipantPresence::Local
        && world.IsRegistered<LocalParticipant>())
    {
        world.AddComponent<LocalParticipant>(participant, {});
    }

    if (Policy.BuildParticipant)
        Policy.BuildParticipant(world, participant);

    return ParticipantAdmission{
        .Status = ParticipantAdmissionStatus::Created,
        .Participant = participant,
    };
}

ParticipantControlChange ParticipantLifecycle::SetControlSubject(
    World& world, EntityId participant, EntityId subject)
{
    ParticipantControlChange change;
    change.Participant = participant;

    if (!TracksParticipants(world) || !world.IsAlive(participant))
        return change;

    ParticipantControl* control = world.TryGet<ParticipantControl>(participant);
    if (control == nullptr)
        return change;

    const EntityId current = subject.IsValid() && world.IsAlive(subject)
        ? subject
        : EntityId{};
    change.PreviousSubject = control->ControlSubject;
    change.CurrentSubject = current;
    if (change.PreviousSubject == current)
    {
        change.Status = ParticipantControlStatus::Unchanged;
        return change;
    }

    ReleaseInputReference(world, change.PreviousSubject);
    control = world.TryGet<ParticipantControl>(participant);
    if (control == nullptr)
        return change;
    control->ControlSubject = EntityId{};

    if (current.IsValid())
    {
        change.DisplacedParticipant =
            ClearSubjectFromOtherParticipants(world, participant, current);
        if (!BindSubject(world, participant, current))
            return change;
    }

    if (world.IsRegistered<LocalParticipant>()
        && world.HasComponent<LocalParticipant>(participant))
    {
        SetLocalControlSubject(world, current);
    }

    change.Status = ParticipantControlStatus::Changed;
    return change;
}

ParticipantBodyChange ParticipantLifecycle::RequestBody(
    World& world, EntityId participant)
{
    ParticipantBodyChange result;
    result.Participant = participant;
    if (!world.IsAlive(participant) || !Policy.ProvideBody)
    {
        result.Status = world.IsAlive(participant)
            ? ParticipantBodyStatus::Unavailable
            : ParticipantBodyStatus::InvalidParticipant;
        return result;
    }

    const ParticipantControl* held = world.TryGet<ParticipantControl>(participant);
    if (held == nullptr)
        return result;
    if (held->Body.IsValid() && world.IsAlive(held->Body))
    {
        result.Status = ParticipantBodyStatus::AlreadyAssigned;
        result.Body = held->Body;
        return result;
    }

    const EntityId body = Policy.ProvideBody(world, participant);
    if (!body.IsValid() || !world.IsAlive(body))
    {
        result.Status = ParticipantBodyStatus::Unavailable;
        return result;
    }

    result.Control = SetControlSubject(world, participant, body);
    if (ParticipantControl* bound = world.TryGet<ParticipantControl>(participant))
        bound->Body = body;

    result.Status = ParticipantBodyStatus::Assigned;
    result.Body = body;
    return result;
}

ParticipantRetirement ParticipantLifecycle::Retire(
    World& world, EntityId participant)
{
    ParticipantRetirement result;
    result.Participant = participant;
    if (!TracksParticipants(world) || !participant.IsValid()
        || !world.IsAlive(participant)
        || world.TryGet<ParticipantControl>(participant) == nullptr)
    {
        return result;
    }

    EntityId body;
    EntityId subject;
    if (const ParticipantControl* held = world.TryGet<ParticipantControl>(participant))
    {
        body = held->Body;
        subject = held->ControlSubject;
    }

    bool reap = body.IsValid() && world.IsAlive(body);
    if (reap && Policy.ReapBody)
        reap = Policy.ReapBody(world, participant, body);

    (void)SetControlSubject(world, participant, EntityId{});
    world.DestroyEntity(participant);

    if (reap && world.IsAlive(body))
    {
        world.DestroyEntity(body);
        result.BodyReaped = true;
    }

    result.Status = ParticipantRetirementStatus::Retired;
    result.ReleasedSubject = subject;
    result.Body = body;
    return result;
}
