#pragma once

#include <anim/AnimationClipHandle.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTypeId.h>

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
struct SENCHA_COMPONENT("AnimationClipPlayer")
       SENCHA_SCHEMA("AnimationClipPlayer")
       SENCHA_SCENE_CHUNK("ACLP")
AnimationClipPlayerComponent
{
    SENCHA_FIELD("clip")
    SENCHA_ASSET(AnimationClip)
    SENCHA_LABEL("Animation")
    SENCHA_TOOLTIP("The clip this entity plays on its skinned mesh.")
    AnimationClipHandle Clip;

    // Where playback currently sits, in seconds from the clip's start.
    SENCHA_FIELD("time_seconds")
    SENCHA_LABEL("Time")
    SENCHA_TOOLTIP("Current playback position, in seconds from the clip's "
                   "start.")
    float TimeSeconds = 0.0f;

    // Playback speed multiplier. Zero freezes the pose at TimeSeconds, which
    // is how a scene authors a fixed pose (and how a golden capture stays
    // deterministic under a wall-clock tick accumulator).
    SENCHA_FIELD("rate")
    SENCHA_LABEL("Speed")
    SENCHA_TOOLTIP("Playback speed multiplier. Zero holds the pose at Time, "
                   "which is how a scene authors a fixed pose.")
    float Rate = 1.0f;

    SENCHA_FIELD("loop")
    SENCHA_LABEL("Loop")
    SENCHA_TOOLTIP("Wraps back to the start at the clip's end; off holds the "
                   "final pose.")
    bool Loop = true;
};
SENCHA_COMPONENT_DECLARES_TRAITS(AnimationClipPlayerComponent);

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimationClipPlayerComponent.sencha.h>
#endif
