#pragma once

#include <attributes/AttributeId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <vector>

//=============================================================================
// EffectDefinition, authored data.
//
// Modifier application by duration policy:
//   - Instant            : modifiers applied once to Base.
//   - Duration/Infinite,
//     Period == 0        : modifiers folded into Current while active (buffs).
//   - Duration/Infinite,
//     Period  > 0        : modifiers applied to Base every Period (DoT/HoT).
// Granted tags are added to the target for the lifetime of a Duration/Infinite
// effect and revoked on expiry (ref-counted, so stacks survive partial removal).
//=============================================================================
enum class EffectDuration : std::uint8_t
{
    Instant,
    Duration,
    Infinite,
};

enum class ModifierOp : std::uint8_t
{
    Add,
    Multiply,
    Override,
};

struct EffectModifier
{
    AttributeId Attr;
    ModifierOp Op = ModifierOp::Add;
    float Magnitude = 0.0f;
};

struct EffectDefinition
{
    EffectDuration Duration = EffectDuration::Instant;
    float DurationSeconds = 0.0f; // used when Duration == Duration
    float Period = 0.0f;          // 0 = not periodic

    std::vector<EffectModifier> Modifiers;
    std::vector<GameplayTagId> GrantedTags; // applied while a Duration/Infinite effect is active
};
