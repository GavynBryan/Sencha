#pragma once

#include <ecs/EntityId.h>

#include <cstdint>
#include <string>
#include <vector>

class World;

enum class ParticipantInvariantIssue : std::uint8_t
{
    DuplicateLocalParticipant,
    DuplicateControlSubject,
    DeadControlSubject,
    MissingInputReference,
    UnexpectedLocalInputReference,
    WrongInputSource,
    LocalControlMismatch,
};

struct ParticipantInvariantFailure
{
    ParticipantInvariantIssue Issue;
    EntityId Participant;
    EntityId Subject;
};

struct ParticipantValidation
{
    std::vector<ParticipantInvariantFailure> Failures;

    [[nodiscard]] bool Ok() const { return Failures.empty(); }
};

// On-demand only. Walks participant state and explains broken invariants; no
// frame path calls it and no counters are maintained to make it cheap.
[[nodiscard]] ParticipantValidation ValidateParticipants(const World& world);
[[nodiscard]] std::string FormatParticipantStatus(const World& world);
