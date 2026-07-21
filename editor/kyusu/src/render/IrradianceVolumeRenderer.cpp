#include "IrradianceVolumeRenderer.h"

#include "EditorTheme.h"
#include "OverlayBoxEdges.h"

#include "document/EditorScene.h"
#include "selection/SelectionService.h"

#include <math/spatial/GridTransform3d.h>
#include <render/IrradianceVolumeComponent.h>

#include <algorithm>
#include <vector>

namespace
{
// A denser lattice than this stops drawing points (the bounds box remains);
// past a few thousand markers the cloud reads as noise anyway.
constexpr std::uint32_t kMaxDrawnProbes = 4096;

void AppendProbeMarker(std::vector<EditorLineVertex>& vertices, const Vec3d& position,
                       float halfSize, const Vec4& color)
{
    const Vec3d axes[3] = { { halfSize, 0.0f, 0.0f },
                            { 0.0f, halfSize, 0.0f },
                            { 0.0f, 0.0f, halfSize } };
    for (const Vec3d& axis : axes)
    {
        vertices.push_back(EditorLineVertex{ .Position = position - axis, .Color = color });
        vertices.push_back(EditorLineVertex{ .Position = position + axis, .Color = color });
    }
}
}

IrradianceVolumeRenderer::IrradianceVolumeRenderer(SelectionService& selection,
                                                   EditorWideLinePipeline& wideLines,
                                                   EditorLinePipeline& lines)
    : Selection(selection)
    , WideLines(wideLines)
    , Lines(lines)
{
}

void IrradianceVolumeRenderer::DrawViewport(const FrameContext& frame,
                                            const EditorViewport& viewport,
                                            const EditorScene& scene)
{
    const World& world = scene.GetRegistry().Components;
    if (!world.IsRegistered<IrradianceVolumeComponent>())
        return;

    std::vector<EntityId> selected;
    for (const SelectableRef& ref : Selection.GetSelection())
        if (ref.Registry == scene.GetRegistry().Id)
            selected.push_back(ref.Entity);
    const auto isSelected = [&](EntityId entity)
    {
        return std::find(selected.begin(), selected.end(), entity) != selected.end();
    };

    std::vector<EditorLineSegment> segments;
    std::vector<EditorLineVertex> points;
    for (EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityVisible(entity))
            continue;
        const IrradianceVolumeComponent* volume =
            world.TryGet<IrradianceVolumeComponent>(entity);
        if (volume == nullptr)
            continue;
        const Transform3f* transform = scene.TryGetTransform(entity);
        if (transform == nullptr)
            continue;

        // The bake lattice is world-axis-aligned from the entity position and
        // half extents only (rotation and scale do not apply, matching the
        // cook), so the box is drawn unrotated too rather than lying about
        // where probes will land.
        const Aabb3d bounds = Aabb3d::FromCenterHalfExtent(transform->Position,
                                                           volume->HalfExtents);
        AppendBoxEdges(segments, bounds, EditorTheme::IrradianceVolume,
                       EditorTheme::OverlayLinePixels);

        if (!isSelected(entity) || volume->CellSize <= 0.0f)
            continue;
        const GridTransform3d grid = GridTransform3d::FromBounds(bounds, volume->CellSize);
        if (grid.PointCount() > kMaxDrawnProbes)
            continue;
        const float markerHalf = volume->CellSize * 0.08f;
        for (std::uint32_t z = 0; z < grid.DimsZ; ++z)
            for (std::uint32_t y = 0; y < grid.DimsY; ++y)
                for (std::uint32_t x = 0; x < grid.DimsX; ++x)
                    AppendProbeMarker(points, grid.PointPosition(x, y, z), markerHalf,
                                      EditorTheme::IrradianceProbePoint);
    }

    if (!segments.empty())
        WideLines.Submit(frame, viewport, segments, /*onTop*/ false,
                         "IrradianceVolumeRenderer");
    if (!points.empty())
        Lines.Submit(frame, viewport, points);
}
