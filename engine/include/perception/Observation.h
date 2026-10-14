#pragma once

#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <math/Vec.h>

#include <cstdint>

// Evidence that an observer acquired something on one tick. Not an instruction,
// and not a memory: it describes that tick's scan and nothing after it.
struct Observation
{
    EntityId Observer;
    GameplayTagId Sense;
    // Invalid for a stimulus with no known source.
    EntityId Subject;
    // Where the subject was when sensed. Resolving Subject to its transform
    // now gives where it is, which is a different fact.
    Vec3d Position = Vec3d::Zero();
    std::uint64_t Tick = 0;
    // In [0, 1].
    float Confidence = 1.0f;
};

// Whether a missing observation means anything. Only a Complete scan proves
// that what it did not report was not there.
enum class ScanCoverage : std::uint8_t
{
    NotEvaluated,
    Partial,
    Complete,
};
