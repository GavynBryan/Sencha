#pragma once

#include <abilities/AbilityCapacity.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>
#include <type_traits>

// One declared local's value. Which members mean something is the local's declared
// type: a number for bool, int and float; Point for a vec3; Entity for an entity; all
// of Hit, Entity, Point and Normal for a hit.
struct AbilityLocalValue
{
    bool Set = false;
    bool Hit = false;
    double Number = 0.0;
    Vec3d Point = Vec3d::Zero();
    Vec3d Normal = Vec3d::Zero();
    EntityId Entity;
};

// Activation-local values. Never replicated: a predicting client computes its own.
struct AbilityRecordLocals
{
    AbilityLocalValue Values[kAbilityLocalCapacity] = {};
    // The ability's one entities local.
    std::uint8_t EntityCount = 0;
    EntityId Entities[kAbilityEntityListCapacity] = {};
};
