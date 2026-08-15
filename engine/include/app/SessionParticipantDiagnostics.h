#pragma once

#include <ecs/EntityId.h>

#include <cstdint>
#include <string>
#include <vector>

class World;

enum class SessionParticipantInvariantIssue : std::uint8_t
{
    MissingIdentity,
    ParticipantNotReplicated,
    DuplicatePeerIdentity,
    BodyNotReplicated,
    BodyOwnerMismatch,
    DrivenSubjectMismatch,
    OrphanDrivenSubject,
};

struct SessionParticipantInvariantFailure
{
    SessionParticipantInvariantIssue Issue;
    EntityId Participant;
    EntityId Entity;
    std::uint32_t Peer = 0;
};

struct SessionParticipantValidation
{
    std::vector<SessionParticipantInvariantFailure> Failures;

    [[nodiscard]] bool Ok() const { return Failures.empty(); }
};

// Cross-domain audit of the projection. Kept beside the projection owner rather
// than in net/ or participant/, because neither underlying mechanism may know
// about the other.
[[nodiscard]] SessionParticipantValidation ValidateSessionParticipants(
    const World& world);
[[nodiscard]] std::string FormatSessionParticipantStatus(const World& world);
