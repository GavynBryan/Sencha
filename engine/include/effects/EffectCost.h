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

// One Instant cost, and who pays it.
struct EffectCostRow
{
    EntityId Target;
    EffectId Effect;
};

struct EffectCostCheck
{
    EffectCostStatus Status = EffectCostStatus::Affordable;
    // Who, which cost and which attribute the first failure names.
    EntityId Target{};
    EffectId Effect{};
    AttributeId Attribute{};
    float Needed = 0.0f;
    float Available = 0.0f;

    [[nodiscard]] bool Affordable() const { return Status == EffectCostStatus::Affordable; }
};

// Whether every row can be paid together, without taking any Base below zero or
// below the attribute's minimum, whichever is higher. Deltas are summed per target
// and attribute first; anything missing fails closed. Pure.
[[nodiscard]] EffectCostCheck CheckEffectCosts(const World& world, std::span<const EffectCostRow> costs);
