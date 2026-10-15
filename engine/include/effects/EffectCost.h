#pragma once

#include <attributes/AttributeId.h>
#include <ecs/EntityId.h>
#include <effects/EffectId.h>

#include <cstdint>
#include <span>

class World;

enum class EffectCostStatus : std::uint8_t
{
    Affordable,
    DefinitionMissing,
    // A cost is an Instant effect whose modifiers are all Add; anything else
    // cannot be checked exactly before it is applied.
    NotAnInstantCost,
    AttributeSetMissing,
    AttributeMissing,
    Insufficient,
};

struct EffectCostCheck
{
    EffectCostStatus Status = EffectCostStatus::Affordable;
    // The cost or attribute the first failure names.
    EffectId Effect{};
    AttributeId Attribute{};
    float Needed = 0.0f;
    float Available = 0.0f;

    [[nodiscard]] bool Affordable() const { return Status == EffectCostStatus::Affordable; }
};

// Whether `actor` can pay every cost in `costs` together, without taking any Base
// below zero or below the attribute's minimum, whichever is higher. Deltas are
// summed per attribute first; anything missing fails closed. Pure.
[[nodiscard]] EffectCostCheck CheckEffectCosts(const World& world, EntityId actor,
                                               std::span<const EffectId> costs);
