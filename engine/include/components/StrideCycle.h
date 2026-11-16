#pragma once

#include <ecs/ComponentAnnotations.h>

#include <type_traits>

//=============================================================================
// StrideCycle  (neutral component catalog)
//
// How far through its walking cycle a character is, counted by distance covered
// over the ground it stands on: Position runs from 0 to 1 over one stride, a
// step with each foot, and wraps. Opt-in: a character whose content carries one
// has its strides counted, and no other character pays for it. Movement writes
// it once a tick; it lives here so a camera's bob and a rig's cycle read one
// clock without depending on movement. The previous tick's values are kept so a
// frame between ticks can be drawn from a blend of the two. Nothing in it is
// authored, so its schema has no fields: presence is the request.
//=============================================================================
struct SENCHA_COMPONENT("sencha.stride_cycle")
       SENCHA_SCHEMA("StrideCycle")
       SENCHA_SCENE_CHUNK("STRD")
StrideCycle
{
    float Position = 0.0f;
    float PreviousPosition = 0.0f;
    // Speed across the supporting ground, in metres per second; zero off it and
    // while no stride is counted.
    float GroundSpeed = 0.0f;
    float PreviousGroundSpeed = 0.0f;
};

static_assert(std::is_trivially_copyable_v<StrideCycle>,
              "StrideCycle must be trivially copyable to live in ECS chunks");

// Position `alpha` of the way from the previous tick to this one, the short way
// round the wrap. A tick advances it by less than half a stride.
[[nodiscard]] inline float StrideCyclePositionAt(const StrideCycle& stride, float alpha)
{
    float advance = stride.Position - stride.PreviousPosition;
    if (advance < 0.0f)
        advance += 1.0f;
    const float at = stride.PreviousPosition + advance * alpha;
    return at >= 1.0f ? at - 1.0f : at;
}

[[nodiscard]] inline float StrideGroundSpeedAt(const StrideCycle& stride, float alpha)
{
    return stride.PreviousGroundSpeed + (stride.GroundSpeed - stride.PreviousGroundSpeed) * alpha;
}

#if !defined(SENCHA_CODEGEN)
#  include <components/StrideCycle.sencha.h>
#endif
