#pragma once

#include <span>

#include <math/Mat.h>

struct MeshGeometry;
class BakeBvh;
struct BakeDirectLight;
struct DirectLightBakeParams;

// Bake-time adaptive-tessellation controls (renderer plan 7A.5, re-gated on
// baked-light proximity). Defaults suit small accent lights on sparse brush
// faces; tune per level.
struct DirectTessellationParams
{
    // An edge is a split candidate only if its midpoint is within
    // (light range + this margin) of some baked light. Keeps open floor far
    // from any light untouched.
    float LightProximityMargin = 1.0f;
    // Split when the baked radiance at the edge midpoint differs from the
    // interpolated endpoint radiance by more than this (per channel).
    float ErrorTolerance = 0.15f;
    int MaxDepth = 3;
    // Never split an edge shorter than this (world units).
    float MinEdgeLength = 0.25f;
    // Midpoint position quantum (world units). Deterministic, so a shared edge
    // lands the same midpoint on both sides.
    float PositionQuantum = 0.001f;
    // Refinement stops once vertices reach this multiple of the base count.
    float MaxVertexGrowth = 8.0f;
};

// Subdivide render triangles near baked lights so the direct-light falloff is
// captured on sparse faces. Splits an edge only where the baked radiance at its
// midpoint diverges from the interpolated endpoints and the edge is near a
// light. The split decision and midpoint are deterministic functions of the
// edge, so edges shared within a face subdivide conformingly (no T-junctions).
// Adds render-only vertices/indices in place; run before BakeDirectLighting.
// Returns the vertex count added.
std::size_t TessellateForDirectBake(MeshGeometry& geometry,
                                    const Mat4& worldTransform,
                                    std::span<const BakeDirectLight> lights,
                                    const BakeBvh& occluders,
                                    const DirectLightBakeParams& bakeParams,
                                    const DirectTessellationParams& tessParams);
