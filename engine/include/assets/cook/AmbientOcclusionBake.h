#pragma once

#include <math/Vec.h>

#include <span>

class BakeBvh;

//=============================================================================
// Ambient-occlusion sample evaluator. Dev-only (SENCHA_ENABLE_COOK), pure.
//
// Cosine-weighted hemisphere visibility over a fixed unit-sphere ray table:
// rays on the sample's upper hemisphere test a fixed-length segment against
// the bake BVH (two-sided, like all bake occlusion), and the result is the
// visible fraction weighted by each ray's cosine. Deterministic: the table
// is fixed and rays evaluate in table order. The result modulates the
// runtime ambient term only; direct light and shadows are never touched.
//=============================================================================

struct AmbientOcclusionBakeParams
{
    // Rays unblocked within this distance count as open; contact darkening
    // reaches roughly this far from occluding geometry.
    float MaxDistance = 1.0f;
    // Ray-origin lift along the sample normal, mirroring the radiance bake's
    // receiver offset so a sample's own surface is not its occluder.
    float NormalOffset = 0.02f;
    // Fixed unit directions (BuildProbeRayTable); rays below the sample's
    // horizon are skipped, so roughly half the table contributes per sample.
    std::span<const Vec3d> RayTable;
};

// Visible fraction in [0, 1]: 1 = fully open, 0 = fully enclosed.
[[nodiscard]] float EvaluateAmbientOcclusion(const Vec3d& position,
                                             const Vec3d& normal,
                                             const BakeBvh& occluders,
                                             const AmbientOcclusionBakeParams& params);
