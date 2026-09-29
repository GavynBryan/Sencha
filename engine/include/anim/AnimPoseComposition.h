#pragma once

#include <anim/AnimRigData.h>
#include <anim/Skeleton.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <span>
#include <vector>

struct AnimPoseLayer
{
    // One local transform per joint; empty applies nothing.
    std::span<const Transform3f> Pose;
    // Additive only: what the pose is measured from.
    std::span<const Transform3f> Reference;
    float Weight = 1.0f;
    AnimLayerMode Mode = AnimLayerMode::Override;
    // Per skeleton joint, nonzero where the layer applies; empty covers every joint.
    std::span<const std::uint8_t> Mask;
};

// One local transform per joint: bind, then each layer in order. O(layers x joints).
void ComposeAnimPose(const SkeletonData& skeleton, std::span<const AnimPoseLayer> layers,
                     std::vector<Transform3f>& out);

void AnimBindPose(const SkeletonData& skeleton, std::vector<Transform3f>& out);
