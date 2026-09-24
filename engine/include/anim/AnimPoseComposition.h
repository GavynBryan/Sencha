#pragma once

#include <anim/AnimRigData.h>
#include <anim/Skeleton.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// Layer pose composition
//
// A rig's layers, composed in declared order into one local pose. The pose
// starts at bind; each layer then applies its own pose -- already sampled and
// blended within the layer -- over the joints its mask covers, at its weight:
//
//   Override  blends each covered joint toward the layer's pose, by component
//             (lerp for translation and scale, slerp for rotation).
//   Additive  adds what the layer's pose changes from its reference pose (its
//             content's first frame): the translation difference, the local
//             rotation from the reference to the pose, and the scale ratio,
//             each scaled by the weight. A layer at its reference adds nothing.
//
// Pure over plain values: no entity, no binding, no device.
//=============================================================================

struct AnimPoseLayer
{
    // One local transform per joint; empty applies nothing.
    std::span<const Transform3f> Pose;
    // Additive only: what the pose is measured from.
    std::span<const Transform3f> Reference;
    float Weight = 1.0f;
    AnimLayerMode Mode = AnimLayerMode::Override;
    // One entry per skeleton joint, nonzero where the layer applies; empty
    // covers every joint.
    std::span<const std::uint8_t> Mask;
};

// Fills `out` with one local transform per skeleton joint: bind, then each
// layer in order. O(layers x joints).
void ComposeAnimPose(const SkeletonData& skeleton, std::span<const AnimPoseLayer> layers,
                     std::vector<Transform3f>& out);

// The skeleton's bind pose as local transforms.
void AnimBindPose(const SkeletonData& skeleton, std::vector<Transform3f>& out);
