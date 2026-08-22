#pragma once

#include <render/LightGpuTypes.h>
#include <render/ProjectedShadowTypes.h>

#include <span>
#include <vector>

//=============================================================================
// Projected-shadow direction policy (pure).
//
// A caster grounds along one direction. It comes from the lights actually
// affecting the caster -- the blob under a character walking past a lamp
// swings away from the lamp -- and from an authored fallback when nothing
// dominates, which is also what an unlit room and the outdoors use. This is
// late-Source's own matured behaviour (r_shadowfromworldlights over a
// shadow_control default).
//
// Two properties are load-bearing:
//  - Continuity. The target is an intensity-weighted BLEND over every
//    contributing light plus a fixed fallback floor, not a winner-take-all
//    pick, so two lights swapping dominance cannot pop the direction.
//    Weights follow the forward shader's own attenuation shape (inverse
//    square times the (d/r)^4 window, cone term for spots), squared to let
//    the strongest light dominate while staying continuous.
//  - Determinism. Fixed iteration over the packed light array, retained state
//    ordered by RenderEntityKey, and an explicit dt: the same caster, lights,
//    and dt sequence produce the same directions on every run.
//=============================================================================

struct ProjectedShadowDirectionParams
{
    // Unit direction used when no light dominates; also the blend floor.
    Vec<3> FallbackDirection = Vec<3>(0.0f, -1.0f, 0.0f);
    // The fallback's constant weight in the blend. Lights whose attenuated
    // weight is far above this own the direction; far below, the fallback
    // does. Zero would let a single guttering light spin the blob forever.
    float FallbackWeight = 0.05f;
    // Exponential smoothing rate (per second). The blend converges as
    // 1 - exp(-rate * dt): higher snaps, lower drifts.
    float SmoothingRate = 8.0f;
    // Minimum downward pitch, degrees below horizontal. A grounding shadow
    // whose direction points up or skims the floor cannot ground -- a light
    // below the caster's center (floor lamp, muzzle flash, a low sconce next
    // to a tall caster) would otherwise paint the caster's bounds as a
    // featureless slab across the floor. The blend is clamped onto this cone;
    // a near-vertical-up blend has no horizontal course to keep and takes the
    // fallback instead.
    float MinPitchDegrees = 20.0f;
    // Frames a caster may go unseen before its smoothing state is dropped.
    std::uint32_t EvictAfterFrames = 300;
};

// The unsmoothed target direction for one caster position. Exposed separately
// so the blend is testable without retained state.
[[nodiscard]] Vec<3> ProjectedShadowTargetDirection(
    std::span<const GpuLight> lights,
    const Vec<3>& casterCenter,
    const ProjectedShadowDirectionParams& params);

// Smooths every caster in `set` toward its target, updating `state` in place:
// matches by key, converges by dt, ages and evicts entries for casters no
// longer present. `state` stays sorted by key; casters keep extraction order.
void UpdateProjectedShadowDirections(ProjectedShadowSet& set,
                                     std::span<const GpuLight> lights,
                                     std::vector<ProjectedShadowDirectionState>& state,
                                     float dt,
                                     const ProjectedShadowDirectionParams& params);
