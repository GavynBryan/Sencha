#pragma once

#include <abilities/AbilityBindingNames.h>

#include <array>

// The persisted names of an ability value's choices: the ports a value reads,
// the types a local holds, and the fields of a hit.
inline constexpr std::array<AbilityNamed<AbilityPort>, 9> kAbilityPortNames{ {
    { "actor", AbilityPort::Actor },
    { "instigator", AbilityPort::Instigator },
    { "aim.origin", AbilityPort::AimOrigin },
    { "aim.direction", AbilityPort::AimDirection },
    { "phase.progress", AbilityPort::PhaseProgress },
    { "phase.ticks", AbilityPort::PhaseTicks },
    { "pulse", AbilityPort::Pulse },
    { "time.scale", AbilityPort::TimeScale },
    { "target", AbilityPort::Target },
} };

inline constexpr std::array<AbilityNamed<AbilityLocalType>, 7> kAbilityLocalTypeNames{ {
    { "bool", AbilityLocalType::Bool },
    { "int", AbilityLocalType::Int },
    { "float", AbilityLocalType::Float },
    { "vec3", AbilityLocalType::Vec3 },
    { "entity", AbilityLocalType::Entity },
    { "hit", AbilityLocalType::Hit },
    { "entities", AbilityLocalType::Entities },
} };

inline constexpr std::array<AbilityNamed<AbilityHitField>, 3> kAbilityHitFieldNames{ {
    { "entity", AbilityHitField::Entity },
    { "point", AbilityHitField::Point },
    { "normal", AbilityHitField::Normal },
} };
