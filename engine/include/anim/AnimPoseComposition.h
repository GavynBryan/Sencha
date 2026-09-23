#pragma once

#include <anim/AnimRigData.h>
#include <anim/AnimationClip.h>
#include <anim/Skeleton.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// Layer pose composition
//
// A rig's layers, composed in declared order into one local pose. The pose
// starts at bind; each layer then samples its clip and applies it over the
// joints its mask covers, at its weight:
//
//   Override  blends each covered joint toward the sample, by component
//             (lerp for translation and scale, slerp for rotation).
//   Additive  adds what the sample changes from the clip's own first frame:
//             the translation difference, the local rotation from the first
//             frame's to the sample's, and the scale ratio, each scaled by
//             the weight. A layer at its first frame therefore adds nothing.
//
// Pure over plain values: no entity, no binding, no device. What a layer
// plays, when and how strongly, is the caller's to resolve.
//=============================================================================

struct AnimPoseLayer
{
    // Nothing to apply when null.
    const AnimationClipData* Clip = nullptr;
    float TimeSeconds = 0.0f;
    float Weight = 1.0f;
    AnimLayerMode Mode = AnimLayerMode::Override;
    // One entry per skeleton joint, nonzero where the layer applies; empty
    // covers every joint.
    std::span<const std::uint8_t> Mask;
};

// Samples reused across calls, so a composition per frame allocates nothing
// once warm.
struct AnimPoseScratch
{
    std::vector<Transform3f> Sample;
    std::vector<Transform3f> Reference;
};

// Fills `out` with one local transform per skeleton joint. O(layers x joints)
// plus the sampling; an additive layer samples its clip twice.
void ComposeAnimPose(const SkeletonData& skeleton, std::span<const AnimPoseLayer> layers, AnimPoseScratch& scratch,
                     std::vector<Transform3f>& out);
