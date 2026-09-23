#pragma once

#include <anim/AnimRigBinding.h>

#include <array>
#include <cstdint>
#include <span>

//=============================================================================
// Blendspace evaluation
//
// Where a blendspace sits and how its samples mix there, as pure functions of
// the bound blendspace and the facts, shared by content resolution (which
// advances the phase), the pose (which samples the mix) and the editor (which
// draws it).
//
// Weights are gradient-band weights: each sample's weight falls off linearly
// toward every other sample and is the least of those falloffs, then all are
// normalized. On one axis that is linear interpolation between neighbours; on
// two it covers any layout without a triangulation, and a point on a sample
// gives that sample alone.
//=============================================================================

using AnimBlendspacePoint = std::array<float, 2>;

// The facts' point, clamped to each axis. A second coordinate is 0 on a
// one-axis blendspace.
[[nodiscard]] AnimBlendspacePoint AnimBlendspaceCoordinates(const AnimBoundBlendspace& space,
                                                            std::span<const std::uint32_t> facts,
                                                            const AnimBoundRig& rig);

// One weight per sample into `weights` (at least the sample count), summing
// to one.
void AnimBlendspaceWeights(const AnimBoundBlendspace& space, AnimBlendspacePoint at, std::span<float> weights);

// The mix's length at these weights: the weighted mean of the samples'.
[[nodiscard]] float AnimBlendspaceDuration(const AnimBoundRig& rig, const AnimBoundBlendspace& space,
                                           std::span<const float> weights);

// The heaviest sample, the lowest index on a tie: whose events the mix plays.
[[nodiscard]] std::size_t AnimBlendspaceDominant(const AnimBoundBlendspace& space, std::span<const float> weights);
