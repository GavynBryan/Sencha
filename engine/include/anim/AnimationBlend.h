#pragma once

#include <anim/SkeletonPose.h>

#include <span>

void BlendPoses(std::span<const JointTransform> a,
                std::span<const JointTransform> b,
                float alpha,
                std::span<JointTransform> out);
void BlendPosesWeighted(std::span<const std::span<const JointTransform>> poses,
                        std::span<const float> weights,
                        std::span<JointTransform> out);
void BlendPosesMasked(std::span<const JointTransform> base,
                      std::span<const JointTransform> layer,
                      float alpha,
                      std::span<const float> jointWeights,
                      std::span<JointTransform> out);
