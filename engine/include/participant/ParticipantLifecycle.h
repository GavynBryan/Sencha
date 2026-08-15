#pragma once

#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <input/InputActionSource.h>
#include <participant/ParticipantControl.h>

#include <cstdint>
#include <functional>

class World;

struct ParticipantPolicies
{
    // Game-owned participant composition. Called exactly once, after creation.
    std::function<void(World&, EntityId participant)> BuildParticipant;
    // Returns a body when the game currently owes one, or none. Never polled.
    std::function<EntityId(World&, EntityId participant)> ProvideBody;
    // Whether retirement destroys the body. Absent means yes.
    std::function<bool(World&, EntityId participant, EntityId body)> ReapBody;
};

enum class ParticipantAdmissionStatus : std::uint8_t
{
    Created,
    Existing,
    Unavailable,
};

struct ParticipantAdmission
{
    ParticipantAdmissionStatus Status = ParticipantAdmissionStatus::Unavailable;
    EntityId Participant;

    [[nodiscard]] bool Created() const
    {
        return Status == ParticipantAdmissionStatus::Created;
    }
};

enum class ParticipantControlStatus : std::uint8_t
{
    Changed,
    Unchanged,
    InvalidParticipant,
};

struct ParticipantControlChange
{
    ParticipantControlStatus Status = ParticipantControlStatus::InvalidParticipant;
    EntityId Participant;
    EntityId PreviousSubject;
    EntityId CurrentSubject;
    EntityId DisplacedParticipant;

    [[nodiscard]] bool Changed() const
    {
        return Status == ParticipantControlStatus::Changed;
    }
};

enum class ParticipantBodyStatus : std::uint8_t
{
    Assigned,
    AlreadyAssigned,
    Unavailable,
    InvalidParticipant,
};

struct ParticipantBodyChange
{
    ParticipantBodyStatus Status = ParticipantBodyStatus::InvalidParticipant;
    EntityId Participant;
    EntityId Body;
    ParticipantControlChange Control;
};

enum class ParticipantRetirementStatus : std::uint8_t
{
    Retired,
    NotFound,
};

struct ParticipantRetirement
{
    ParticipantRetirementStatus Status = ParticipantRetirementStatus::NotFound;
    EntityId Participant;
    EntityId ReleasedSubject;
    EntityId Body;
    bool BodyReaped = false;
};

// Owns the generic participant invariant. It has no knowledge of sessions,
// peers, ownership, replication, or prediction; the app-level session
// projection consumes the structured outcomes and adds those facts.
class ParticipantLifecycle
{
public:
    [[nodiscard]] ParticipantPolicies& Policies() { return Policy; }
    [[nodiscard]] const ParticipantPolicies& Policies() const { return Policy; }

    // Creates a participant, or returns the existing local participant when
    // `presence` is Local. Structural. BuildParticipant runs only for Created.
    ParticipantAdmission Admit(
        World& world,
        InputActionSourceId source,
        ParticipantPresence presence = ParticipantPresence::Simulated,
        StoragePartitionId partition = StoragePartitionId{});

    // The explicit respawn/content-ready seam. A living body makes this an
    // AlreadyAssigned no-op; an unavailable policy answer is not retried.
    ParticipantBodyChange RequestBody(World& world, EntityId participant);

    // Moves this participant's input and local-presentation projection as one
    // operation. A subject has at most one participant; transfer clears the
    // prior holder. Structural and O(participants).
    ParticipantControlChange SetControlSubject(
        World& world, EntityId participant, EntityId subject);

    // Releases control, applies the body-reap policy, and destroys only the
    // participant and (when approved) its body. The control subject is never
    // destroyed. Structural.
    ParticipantRetirement Retire(World& world, EntityId participant);

private:
    ParticipantPolicies Policy;
};

// Read-only, O(participants).
[[nodiscard]] EntityId LocalParticipantOf(const World& world);
