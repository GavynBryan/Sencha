#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <math/Vec.h>

#include <cstdint>

inline constexpr std::uint32_t kObservationMemoryCapacity = 8;

// What an observer last knew about one subject. An anonymous entry (invalid
// Subject) is a place something was sensed, such as a sound.
struct ObservationMemoryEntry
{
    EntityId Subject;
    GameplayTagId Sense;
    Vec3d Position = Vec3d::Zero();
    // Zero unless two observations arrived close enough together to measure.
    Vec3d Velocity = Vec3d::Zero();
    float Confidence = 0.0f;
    // Seconds since the subject was last observed.
    float Age = 0.0f;
    // Seconds the subject has been observed without a lapse.
    float Tracked = 0.0f;
};

// An observer's retained observations, updated on the authority after each
// perception tick. Runtime entries are neither authored nor replicated.
struct SENCHA_COMPONENT("ObservationMemory")
       SENCHA_SCHEMA("ObservationMemory")
       SENCHA_SCENE_CHUNK("OBSM")
ObservationMemory
{
    SENCHA_FIELD("retain_seconds")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Retain seconds")
    SENCHA_TOOLTIP("How long something unobserved is remembered.")
    float RetainSeconds = 8.0f;

    std::uint32_t Count = 0;
    ObservationMemoryEntry Entries[kObservationMemoryCapacity]{};
};

[[nodiscard]] const ObservationMemoryEntry* FindRemembered(const ObservationMemory& memory, EntityId subject);

#if !defined(SENCHA_CODEGEN)
#  include <perception/ObservationMemory.sencha.h>
#endif
