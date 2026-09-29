#pragma once

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimPlayback.h>
#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

class World;

// Enough to pose what a layer plays again at another tick.
struct AnimLayerPlayback
{
    AnimPlayback Time;
    // The tick Phase was read at.
    AnimTick PhaseTick = 0;
    float Phase = 0.0f;
    // Normalized phase per second.
    float PhaseRate = 0.0f;
    float Coordinates[2] = {};
    GameplayTagId Behavior;
    std::uint16_t Content = kAnimNoContent;
    std::uint16_t Clip = kAnimNoContent;
};

struct AnimLayerPose
{
    // What the layer was posed from last tick; a change to it is a transition.
    AnimLayerPlayback Playing;
    // A crossfade's outgoing playback, posed beside Playing until it fades.
    AnimLayerPlayback FadingOut;
    AnimTick FadeStartTick = 0;
    AnimTick OffsetStartTick = 0;
    float FadeInSeconds = 0.0f;
    float FadeOutSeconds = 0.0f;
    // The longest decaying offset's duration; the offsets are spent once it passes.
    float OffsetSeconds = 0.0f;
    bool Fading = false;
    bool Offsetting = false;
    bool Posed = false;
};

inline constexpr std::uint32_t kAnimNoPoseSlot = 0;

// Carried only where a pose is presented; selection, content and events never read
// it. The per-joint half lives in the AnimPosePool slot it names.
struct SENCHA_COMPONENT("sencha.anim_pose_state") AnimPoseState
{
    AnimLayerPose Layers[kAnimMaxLayers] = {};
    // The rig binding the layers were posed against; another resets them.
    std::uint64_t BindingGeneration = 0;
    // One past the pool slot index; kAnimNoPoseSlot before one is assigned.
    std::uint32_t Slot = kAnimNoPoseSlot;
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimPoseState.sencha.h>
#endif

template <>
struct ComponentTraits<AnimPoseState>
{
    static void OnRemove(const AnimPoseState& state, World& world, EntityId entity);
};
