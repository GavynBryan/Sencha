#pragma once

#include "BrushMesh.h"

#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <vector>

//=============================================================================
// BrushLightmapCharts — groups a brush's faces into lightmap charts. A chart
// is a run of faces connected through SOFT edges (the same authored softness
// that drives smooth shading normals), split when the group's normal spread
// exceeds a cone limit so the planar projection stays low-distortion. Hard
// edges always cut charts, so a hard-edged box is one chart per face.
//
// Chart UVs are world-unit distances in the chart's best-fit plane; the level
// cook scales them by the luxel size and packs charts into the zone atlas.
// Deterministic: growth seeds and BFS order follow ascending face index, and
// soft-edge adjacency follows the stored SoftEdges order.
//=============================================================================

struct BrushLightmapChart
{
    Vec3d Normal;   // area-weighted chart normal (world space, normalized)
    Vec3d TangentU; // chart plane basis, world space
    Vec3d TangentV;
    Vec2d UvMin;    // chart-space AABB, world units; min floored to luxel
    Vec2d UvMax;    //   multiples so coplanar charts share luxel grid phase
};

struct BrushChartSet
{
    std::vector<BrushLightmapChart> Charts;
    // Per brush face -> index into Charts. Degenerate faces (loop < 3 or zero
    // normal) still receive a singleton chart so every emitted triangle has one.
    std::vector<std::uint32_t> FaceChart;
};

// Chart-space UV (world units, relative to the chart AABB min) of a world point.
[[nodiscard]] inline Vec2d BrushChartUv(const BrushLightmapChart& chart, const Vec3d& worldPos)
{
    return Vec2d{ worldPos.Dot(chart.TangentU) - chart.UvMin.X,
                  worldPos.Dot(chart.TangentV) - chart.UvMin.Y };
}

[[nodiscard]] BrushChartSet BuildBrushLightmapCharts(const BrushMesh& mesh,
                                                     const Transform3f& transform,
                                                     float coneDegrees,
                                                     float luxelSize);
