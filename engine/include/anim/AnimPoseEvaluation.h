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

[[nodiscard]] AnimLayerPlayback AnimPlaybackOf(const AnimBoundRig& rig, const AnimLayerContent& layer, AnimTick now);

// Poses `playback` at tick `at`, or the bind pose when it names nothing.
// `reference` poses the first frame instead, which an additive layer adds from.
void SampleAnimPlayback(const AnimPoseSources& sources, const AnimLayerPlayback& playback, AnimTick at,
                        double tickSeconds, bool reference, AnimPoseScratch& scratch, std::vector<Transform3f>& out);

// The offset carrying `target` back to `shown`, with the velocity `shown`
// had since `shownBefore` a tick earlier, decaying over `seconds`.
[[nodiscard]] AnimJointOffset AnimInertializeJoint(const Transform3f& shown, const Transform3f& shownBefore,
                                                   const Transform3f& target, float seconds, double tickSeconds);

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
    AnimDecisionLog* Log = nullptr;
    AnimTick Now = 0;
    double TickSeconds = 0.0;
};

// Writes each layer's pose and the composed pose into the slot, keeping the
// previous composed pose for interpolation.
void EvaluateAnimPose(const AnimPoseInput& input, AnimPoseScratch& scratch);
