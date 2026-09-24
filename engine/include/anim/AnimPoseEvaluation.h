#pragma once

#include <anim/AnimContentState.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimPosePool.h>
#include <anim/AnimPoseState.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimSelectorState.h>
#include <anim/Skeleton.h>
#include <math/geometry/3d/Transform3d.h>

#include <array>
#include <vector>

class AnimationClipCache;

//=============================================================================
// Pose evaluation
//
// One posed entity, one tick: what each layer plays becomes a pose, a change to
// what it plays is absorbed the way its destination's blend policy says, and
// the layers compose into the entity's pose.
//
//   Snap         takes the new pose at once.
//   Crossfade    keeps the outgoing playback posing beside the incoming one:
//                the incoming weight rises over `in`, the outgoing falls over
//                `out`, and the pose blends between them by their share.
//   Inertialize  poses only the incoming content. On the change tick it takes
//                each joint's offset from the incoming pose to what was being
//                shown, and the offset's velocity from the tick before, and
//                decays both to rest over `in` along a quintic. A change while
//                one is decaying starts from what was shown, offset included,
//                so stacked changes fold into one offset.
//
// Each layer blends inside itself before composition, so a change on one
// layer never disturbs another. Everything here is a function of the content
// state, the pose state and the tick, and touches only this entity's slot.
//=============================================================================

struct AnimPoseSources
{
    const AnimBoundRig* Rig = nullptr;
    const AnimationClipCache* Clips = nullptr;
    const SkeletonData* Skeleton = nullptr;
};

// Buffers one evaluation reuses, one set per worker.
struct AnimPoseScratch
{
    std::vector<Transform3f> Sample;
    std::vector<Transform3f> Mix;
    std::vector<Transform3f> Fading;
    std::vector<Transform3f> Shown;
    std::vector<Transform3f> ShownBefore;
    std::vector<Transform3f> Incoming;
    std::vector<Transform3f> Composed;
    std::array<std::vector<Transform3f>, kAnimMaxLayers> References;
};

// The playback a layer's content describes on tick `now`.
[[nodiscard]] AnimLayerPlayback AnimPlaybackOf(const AnimBoundRig& rig, const AnimLayerContent& layer, AnimTick now);

// Poses `playback` at tick `at`: its clip at its time then, or its
// blendspace's samples mixed at its phase then. Bind when it names nothing.
// `reference` poses its first frame instead, what an additive layer adds from.
void SampleAnimPlayback(const AnimPoseSources& sources, const AnimLayerPlayback& playback, AnimTick at,
                        double tickSeconds, bool reference, AnimPoseScratch& scratch, std::vector<Transform3f>& out);

// The offset carrying `target` back to `shown`, with the velocity `shown`
// had since `shownBefore` a tick earlier, decaying over `seconds`.
[[nodiscard]] AnimJointOffset AnimInertializeJoint(const Transform3f& shown, const Transform3f& shownBefore,
                                                   const Transform3f& target, float seconds, double tickSeconds);

// Applies what remains of `offset` after `elapsed` seconds to `pose`.
void AnimApplyJointOffset(const AnimJointOffset& offset, float elapsed, Transform3f& pose);

// The largest translation any of `offsets` still carries `elapsed` seconds in.
[[nodiscard]] float AnimOffsetRemaining(std::span<const AnimJointOffset> offsets, float elapsed);

struct AnimPoseInput
{
    AnimPoseSources Sources;
    const AnimContentState* Content = nullptr;
    const AnimSelectorState* Selection = nullptr;
    AnimPoseState* State = nullptr;
    AnimPosePool::Slot* Slot = nullptr;
    // Null when the entity keeps no log.
    AnimDecisionLog* Log = nullptr;
    AnimTick Now = 0;
    double TickSeconds = 0.0;
};

// Poses one entity for tick `Now` into its slot: each layer's pose, then the
// composed pose, the previous one kept for interpolation.
void EvaluateAnimPose(const AnimPoseInput& input, AnimPoseScratch& scratch);
