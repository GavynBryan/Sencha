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

struct AbilityRequestQueue
{
    std::vector<AbilityRequest> Pending;

    // False when full: the asker hears no rather than a request vanishing.
    [[nodiscard]] bool Push(const AbilityRequest& request)
    {
        if (Pending.size() >= kAbilityRequestCapacity)
            return false;
        Pending.push_back(request);
        return true;
    }
};
