#pragma once

#include <abilities/AbilityId.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>
#include <limits>
#include <type_traits>

inline constexpr std::uint32_t kNoDecisionOption = std::numeric_limits<std::uint32_t>::max();

// What an entity that chooses its own options is doing and knows: its clock, its
// target, the option it chose and the ability running it, where that option sent
// it, and what it last knew of its target once memory let go. The authority's,
// rebuilt each run and never saved. docs/gameplay/decision-agent.md.
struct SENCHA_COMPONENT("sencha.decision.agent") DecisionAgent
{
    bool Started = false;
    float UntilDecision = 0.0f;
    std::uint32_t Decisions = 0;

    EntityId Target;
    std::uint32_t Option = kNoDecisionOption;
    float OptionSeconds = 0.0f;
    AbilityId Running;

    bool HasDestination = false;
    Vec3d Destination = Vec3d::Zero();
    // Where the target was known to be when the destination was chosen.
    Vec3d DestinationTarget = Vec3d::Zero();
    float DestinationSeconds = 0.0f;
    float PositionQuality = 0.0f;
    // The option whose place evaluation chose the destination.
    std::uint32_t PlaceOption = kNoDecisionOption;

    // Options whose place evaluation found nowhere, ruled out until the clock runs down.
    std::uint64_t Unavailable = 0;
    float UnavailableSeconds = 0.0f;

    // Once it has known of a target it pursues it from what it last knew.
    bool Engaged = false;
    Vec3d ContactPosition = Vec3d::Zero();
    Vec3d ContactVelocity = Vec3d::Zero();
    Vec3d ContactFacing = Vec3d::Zero();
    float SecondsSinceContact = 0.0f;
    // Since its own eyes last saw its target.
    float SecondsSinceSeen = 1000.0f;

    // The option whose claim it holds a place in.
    std::uint32_t ClaimOption = kNoDecisionOption;
};

static_assert(std::is_trivially_copyable_v<DecisionAgent>);

#if !defined(SENCHA_CODEGEN)
#  include <logic/decision/DecisionAgent.sencha.h>
#endif
