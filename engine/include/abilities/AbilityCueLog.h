#pragma once

#include <abilities/AbilityCapacity.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <math/Vec.h>

#include <cstdint>
#include <type_traits>

// One discrete cue an ability made: where, at what, and by which activation and
// op, for whatever presents it (docs/gameplay/abilitykit.md, "Presentation").
struct AbilityCueEntry
{
    // Serial order on this actor; zero is an empty entry.
    std::uint16_t Seq = 0;
    GameplayTagId Cue;
    // The activation's start order on the actor and the op that made it.
    std::uint64_t Activation = 0;
    std::uint16_t Op = 0;
    std::uint64_t Tick = 0;
    Vec3d Point = Vec3d::Zero();
    Vec3d Normal = Vec3d::Zero();
    EntityId Target;
};

// The actor's recent discrete cues, newest overwriting oldest. Pulse cues are not
// here: the activation's pulse count and last hit carry them.
struct SENCHA_COMPONENT("sencha.ability_cue_log") AbilityCueLog
{
    AbilityCueEntry Entries[kAbilityCueLogCapacity] = {};
    std::uint16_t NextSeq = 1;
    // How many entries the ring has overwritten.
    std::uint32_t Overwritten = 0;
};

static_assert(std::is_trivially_copyable_v<AbilityCueLog>);

// Appends `entry` under the next serial number, which it returns.
std::uint16_t AppendAbilityCue(AbilityCueLog& log, AbilityCueEntry entry);

// Whether serial `a` comes after `b`, across the u16 wrap.
[[nodiscard]] constexpr bool AbilityCueAfter(std::uint16_t a, std::uint16_t b)
{
    return a != b && static_cast<std::uint16_t>(a - b) < 0x8000;
}

#if !defined(SENCHA_CODEGEN)
#  include <abilities/AbilityCueLog.sencha.h>
#  include <abilities/AbilityCueLogWire.h>
#endif
