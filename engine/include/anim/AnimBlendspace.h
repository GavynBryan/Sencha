#pragma once

#include <anim/AnimRigBinding.h>

#include <array>
#include <cstdint>
#include <span>

// Pure functions shared by content resolution, the pose and the editor. Weights are
// gradient-band weights; see docs/gameplay/animation.md.

using AnimBlendspacePoint = std::array<float, 2>;

// The facts' point, clamped to each axis; the second coordinate is 0 on one axis.
[[nodiscard]] AnimBlendspacePoint AnimBlendspaceCoordinates(const AnimBoundBlendspace& space,
                                                            std::span<const std::uint32_t> facts,
                                                            const AnimBoundRig& rig);

// One weight per sample, summing to one; `weights` holds at least the sample count.
void AnimBlendspaceWeights(const AnimBoundBlendspace& space, AnimBlendspacePoint at, std::span<float> weights);

// The mix's length at these weights: the weighted mean of the samples'.
[[nodiscard]] float AnimBlendspaceDuration(const AnimBoundRig& rig, const AnimBoundBlendspace& space,
                                           std::span<const float> weights);

// The heaviest sample, the lowest index on a tie: whose events the mix plays.
[[nodiscard]] std::size_t AnimBlendspaceDominant(const AnimBoundBlendspace& space, std::span<const float> weights);
