#pragma once

#include <abilities/AbilityCapacity.h>
#include <abilities/AbilityId.h>
#include <authored/VerbId.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <vector>

enum class AbilityRequestKind : std::uint8_t
{
    Activate,
    Cancel,
};

// Starts and cancels asked for by name rather than by input -- scripts, verbs,
// tests -- run in the next ability pass, in the order asked. Admission still
// applies to every start.
struct AbilityRequest
{
    AbilityRequestKind Kind = AbilityRequestKind::Activate;
    EntityId Actor{};
    AbilityId Ability{};
    EntityId Instigator{};
    InvocationId Cause{};
};

// Drained by every ability pass, so it holds what one tick asked for and no asker
// is ever turned away for lack of room.
struct AbilityRequestQueue
{
    std::vector<AbilityRequest> Pending;

    void Push(const AbilityRequest& request) { Pending.push_back(request); }
};
