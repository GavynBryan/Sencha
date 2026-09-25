#pragma once

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimTypes.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

class World;

//=============================================================================
// AnimPoseState
//
// How each layer's pose absorbs changes to what it plays: the playback it was
// last posed from, a crossfade's outgoing playback still alive beside it, and
// when the current blend began. Only a World that presents a pose carries it;
// selection, content and events never read it.
//
// The per-joint half -- inertialization offsets, each layer's pose and the
// composed pose of the last two ticks -- is variable in size and lives in the
// World's AnimPosePool, in the slot this names. The component is removed with
// its slot released.
//=============================================================================

// Enough about what a layer plays to pose it again at another tick: a clip
// from its start tick and offset, or a blendspace from its phase, the rate
// that phase moved at and the point it stood at.
struct AnimLayerPlayback
{
    AnimTick ClipStartTick = 0;
    // The tick Phase was read at.
    AnimTick PhaseTick = 0;
    float ClipOffsetSeconds = 0.0f;
    float ClipRate = 1.0f;
    float Phase = 0.0f;
    // Normalized phase per second.
    float PhaseRate = 0.0f;
    float Coordinates[2] = {};
    GameplayTagId Behavior;
    std::uint16_t Content = kAnimNoContent;
    std::uint16_t Clip = kAnimNoContent;
    // Wraps at its end rather than holding the last frame.
    bool Cyclic = false;
};

struct AnimLayerPose
{
    // What the layer was posed from last tick: a change to it is a
    // transition.
    AnimLayerPlayback Playing;
    // A crossfade's outgoing playback, posed beside Playing until it fades.
    AnimLayerPlayback FadingOut;
    AnimTick FadeStartTick = 0;
    AnimTick OffsetStartTick = 0;
    float FadeInSeconds = 0.0f;
    float FadeOutSeconds = 0.0f;
    // The longest joint offset still decaying, in seconds from its start: the
    // offsets are spent once this has passed.
    float OffsetSeconds = 0.0f;
    bool Fading = false;
    bool Offsetting = false;
    // Playing names something: the layer has been posed at least once.
    bool Posed = false;
};

inline constexpr std::uint32_t kAnimNoPoseSlot = 0;

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
    // The slot's per-joint storage goes back to the pool with the component.
    static void OnRemove(const AnimPoseState& state, World& world, EntityId entity);
};
