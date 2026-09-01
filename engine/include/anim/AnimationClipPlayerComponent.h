#pragma once

#include <anim/AnimationClipHandle.h>
#include <ecs/ComponentTypeId.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// AnimationClipPlayerComponent
//
// Plays one clip on the entity's skinned mesh: the pose source the skinning
// path was built without. Time advances on fixed ticks
// (AnimationClipPlaybackSystem) and the render extract samples whatever
// time the component currently holds, so playback is deterministic under
// catch-up frames and a paused player is a fixed, authorable pose.
//
// Deliberately one clip and no blending. A state graph, transitions, and
// layered modifiers are the animation runtime's business; this is the
// smallest thing that makes a character move, and the seam it leaves is the
// component that a graph would replace rather than extend.
//=============================================================================
struct AnimationClipPlayerComponent
{
    AnimationClipHandle Clip;
    // Where playback currently sits, in seconds from the clip's start.
    float TimeSeconds = 0.0f;
    // Playback speed multiplier. Zero freezes the pose at TimeSeconds, which
    // is how a scene authors a fixed pose (and how a golden capture stays
    // deterministic under a wall-clock tick accumulator).
    float Rate = 1.0f;
    bool Loop = true;
};

SENCHA_DECLARE_COMPONENT_TYPE(AnimationClipPlayerComponent, "AnimationClipPlayer");
SENCHA_COMPONENT_DECLARES_SCHEMA(AnimationClipPlayerComponent);
SENCHA_COMPONENT_DECLARES_TRAITS(AnimationClipPlayerComponent);
