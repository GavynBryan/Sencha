#pragma once

#include <span>

#include <math/Mat.h>

struct MeshGeometry;
struct BakeDirectLight;

// Bake-time tessellation controls. Subdivision is distance-graded and uniform
// near lights: an edge splits while it is longer than the target edge length
// at its midpoint, and the target shrinks toward the light. Refined regions
// therefore converge to a regular lattice, which interpolates a smooth falloff
// without the directional streaking irregular (error-driven) refinement shows.
struct DirectTessellationParams
{
    // An edge is a split candidate only while some point on it is within
    // (light range + this margin) of some baked light. Geometry far from every
    // light is never touched.
    float LightProximityMargin = 1.0f;
    // Target edge length at distance d from a light is
    // max(MinEdgeLength, d * GradingFactor): finer where the falloff is
    // steeper, coarser toward the range edge.
    float GradingFactor = 0.15f;
    float MinEdgeLength = 0.25f;
    int MaxDepth = 6;
    // Midpoint position quantum (world units). Deterministic, so a shared edge
    // lands the same midpoint on both sides.
    float PositionQuantum = 0.001f;
    // Refinement stops once vertices reach this multiple of the base count.
    // A runaway backstop only, never a quality budget: the grading is the real
    // control, and a binding cap starves refinement mid-region (visible
    // artifacts). Low-poly cells have tiny base counts, hence the headroom.
    float MaxVertexGrowth = 256.0f;
};

// Subdivide render triangles near baked lights so the direct-light falloff is
// captured on sparse faces. The split decision and midpoint are pure functions
// of the edge (its endpoints and the light set), so edges shared within a
// section subdivide conformingly (no T-junctions). Adds render-only
// vertices/indices in place; run before BakeDirectLighting. Returns the vertex
// count added.
std::size_t TessellateForDirectBake(MeshGeometry& geometry,
                                    const Mat4& worldTransform,
                                    std::span<const BakeDirectLight> lights,
                                    const DirectTessellationParams& tessParams);
