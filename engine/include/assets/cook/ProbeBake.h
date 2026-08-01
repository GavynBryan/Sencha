#pragma once

#include <assets/cook/BakeBvh.h>
#include <assets/cook/DirectLightBake.h>
#include <math/Vec.h>
#include <math/geometry/3d/Aabb3d.h>
#include <math/spatial/GridTransform3d.h>

#include <cstdint>
#include <span>
#include <vector>

class JobSystem;

//=============================================================================
// ProbeBake
//
// Offline irradiance-probe bake for one volume (renderer plan Section 7): per
// probe, a fixed ray table is traced against the bake BVH; misses contribute
// the hemispheric sky/ground by ray direction, front-face hits contribute one
// bounce of the zone's baked direct lighting (the shared per-sample
// evaluator) scaled by a constant albedo, back-face hits contribute black.
// The result is projected into L1 spherical harmonics per color channel.
// Probes inside geometry (short-ray back-face ratio) are classified invalid
// and filled by 6-neighbor dilation so runtime trilinear sampling needs no
// validity logic; probes no dilation can reach take the hemispheric ambient.
//
// Everything here is deterministic: the ray table is a fixed spherical
// Fibonacci spiral, each probe accumulates its own samples in table order and
// writes only its own slot (so the parallel path is bit-identical to serial),
// and dilation commits each round from the previous round's state.
//=============================================================================

// L1 SH per color channel, coefficient order (c1x, c1y, c1z, c0): the linear
// part sits in xyz so evaluation is one dot against the normal. Coefficients
// are the raw projection (basis constants not premultiplied).
struct ProbeShL1
{
    Vec4 R;
    Vec4 G;
    Vec4 B;
};

struct ProbeBakeParams
{
    // One-bounce surface reflectance applied to hit-point direct lighting
    // (render.bake.albedo).
    float BounceAlbedo = 0.35f;
    // Hemispheric environment for ray misses and unfillable probes; matches
    // the runtime ambient pair this bake replaces.
    Vec3d SkyColor;
    Vec3d GroundColor;
    // Rays that travel this far unblocked count as environment misses.
    float MaxRayDistance = 100.0f;
    // A probe whose short rays (this distance) hit back faces on more than
    // this fraction of the table sits inside geometry and is invalidated.
    float ClassifyRayDistance = 0.5f;
    float ClassifyBackfaceRatio = 0.25f;
    // Shared with the direct-lighting bake so the bounce matches the shader.
    DirectLightBakeParams Shading;
};

struct ProbeVolumeBakeResult
{
    // PointCount() entries in GridTransform3d point order, dilated, upload-ready.
    std::vector<ProbeShL1> Sh;
    // PointCount() entries, 1 = baked valid (not dilated). Diagnostics.
    std::vector<std::uint8_t> Valid;
    std::uint32_t InvalidCount = 0;   // classified inside geometry
    std::uint32_t UnfilledCount = 0;  // fell back to the hemispheric ambient
};

// Fixed unit direction table: a spherical Fibonacci spiral, uniform over the
// sphere, identical for every probe and every run.
std::vector<Vec3d> BuildProbeRayTable(std::uint32_t count);

// Projects the analytic hemispheric environment (ground-to-sky lerp over the
// up axis) into L1 SH. The dilation fallback and the "no probes here" answer.
ProbeShL1 ProjectHemisphericAmbientSh(const Vec3d& sky, const Vec3d& ground);

// Cosine-convolved ambient color for a surface normal (irradiance / pi), the
// value the runtime multiplies into base color where hemispheric ambient is
// used today. Clamped to non-negative per channel.
Vec3d EvaluateProbeShL1(const ProbeShL1& sh, const Vec3d& normal);

// Bakes one volume. `jobs` parallelizes over grid layers when non-null with
// workers; the serial path is the reference and both are bit-identical.
ProbeVolumeBakeResult BakeProbeVolume(const GridTransform3d& grid,
                                      const BakeBvh& occluders,
                                      std::span<const BakeDirectLight> lights,
                                      std::span<const Vec3d> rayTable,
                                      const ProbeBakeParams& params,
                                      JobSystem* jobs = nullptr);

// One neighbor zone's cooked geometry offered as read-only halo occluders.
struct ProbeHaloZone
{
    // Content hash of the zone's cooked geometry; the deterministic ordering
    // key, so two zones sharing a boundary assemble byte-identical inputs.
    std::uint64_t ContentHash = 0;
    Aabb3d Bounds = Aabb3d::Empty();
    std::vector<BakeTriangle> Triangles;
};

// The halo zones whose bounds reach within maxRayDistance of zoneBounds,
// sorted by content hash. This is the exact set AssembleProbeBakeTriangles
// folds in, exposed so a cook driver can hash what the bake will actually
// see: a neighbor edit within reach restales the zone, one beyond does not.
std::vector<const ProbeHaloZone*> SelectProbeHaloZones(
    const Aabb3d& zoneBounds,
    std::span<const ProbeHaloZone> halo,
    float maxRayDistance);

// The bake input for one zone: its own triangles, then every selected halo
// zone's triangles. Order only affects exact hit ties; the content-hash sort
// makes it reproducible anyway.
std::vector<BakeTriangle> AssembleProbeBakeTriangles(
    std::vector<BakeTriangle> zoneTriangles,
    const Aabb3d& zoneBounds,
    std::span<const ProbeHaloZone> halo,
    float maxRayDistance);

// Quantizes baked SH into the .sprobe payload layout: three channel planes
// (R, G, B), each probe a quad of fp16 in (c1x, c1y, c1z, c0) order. One
// straight 3D-texture upload per channel at runtime.
std::vector<std::uint16_t> PackProbeShHalf(std::span<const ProbeShL1> sh);
