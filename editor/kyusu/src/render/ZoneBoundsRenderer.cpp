#include "ZoneBoundsRenderer.h"

#include "EditorTheme.h"
#include "OverlayBoxEdges.h"

#include "document/WorldTagList.h"
#include "document/WorldDocument.h"
#include "viewport/WorldViewSettings.h"

#include <zone/ZoneDemand.h>

#include <cmath>
#include <vector>

ZoneBoundsRenderer::ZoneBoundsRenderer(EditorWideLinePipeline& lines)
    : Lines(lines)
{
}

void ZoneBoundsRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                                      WorldDocument& world, WorldViewSettings& view)
{
    if (!world.IsWorld())
        return;

    // The preview focus follows the perspective (fly) camera; ortho viewports
    // reuse the stored focus so every view tints identically this frame.
    std::vector<ZoneDemandRecord> demand;
    WorldPartitionStreamingConfig previewConfig;
    if (view.StreamingPreview)
    {
        if (viewport.Orientation == ViewportOrientation::Perspective)
        {
            view.PreviewFocus = ResolveFocusZone(world.Manifest(), viewport.Camera.Position,
                                                 view.PreviewFocus);
            view.PreviewFocusPosition = viewport.Camera.Position;
        }
        // A camera outside every zone (the usual editing vantage, floating
        // above the map) previews around the zone being edited instead of
        // showing nothing.
        if (!view.PreviewFocus.IsValid())
            view.PreviewFocus = world.FocusZone();
        const std::vector<std::string> activeTags = SplitWorldTagList(view.PreviewTags);
        previewConfig =
            ResolvePreviewStreamingConfig(world.Manifest(), view.PreviewFocus, view);
        demand = ComputeZoneDemand(world.Manifest(), world.Index(), view.PreviewFocus, {},
                                   previewConfig, &view.PreviewFocusPosition, activeTags);
    }
    const auto demanded = [&](ZoneId zone)
    {
        for (const ZoneDemandRecord& record : demand)
            if (record.Zone == zone)
                return true;
        return false;
    };

    std::vector<EditorLineSegment> segments;
    for (const ZoneHeader& zone : world.Manifest().Zones)
    {
        if (!zone.Bounds.IsValid())
            continue;
        Vec4 color;
        if (view.StreamingPreview)
        {
            color = view.PreviewFocus == zone.Id ? EditorTheme::Selection
                  : demanded(zone.Id)            ? EditorTheme::PreviewDemanded
                                                 : EditorTheme::PreviewUndemanded;
        }
        else
        {
            color = world.FocusZone() == zone.Id ? EditorTheme::Selection
                  : world.IsZoneOpen(zone.Id)    ? EditorTheme::BoundsBox
                                                 : EditorTheme::ContextZoneDim;
        }
        AppendBoxEdges(segments, zone.Bounds, color, EditorTheme::OverlayLinePixels);
    }

    // The proximity horizon around the preview focus position. The demand
    // test is a 3D point-to-box distance, so the horizon is a sphere: three
    // orthogonal rings read from any viewport (top view the ground ring,
    // front/side views the vertical rings) and show the reduced ground-level
    // reach when the preview position floats above the cells.
    if (view.StreamingPreview && previewConfig.Radius > 0.0)
    {
        constexpr int kSegments = 64;
        constexpr float kTau = 6.2831853f;
        const float radius = static_cast<float>(previewConfig.Radius);
        const Vec3d& center = view.PreviewFocusPosition;
        const auto ring = [&](const Vec3d& axisA, const Vec3d& axisB)
        {
            Vec3d previous = center + axisA * radius;
            for (int i = 1; i <= kSegments; ++i)
            {
                const float angle = kTau * static_cast<float>(i) / kSegments;
                const Vec3d point = center + axisA * (radius * std::cos(angle))
                    + axisB * (radius * std::sin(angle));
                segments.push_back(EditorLineSegment{ previous, point,
                                                      EditorTheme::PreviewDemanded,
                                                      EditorTheme::OverlayLinePixels });
                previous = point;
            }
        };
        ring(Vec3d{ 1.0f, 0.0f, 0.0f }, Vec3d{ 0.0f, 0.0f, 1.0f });
        ring(Vec3d{ 1.0f, 0.0f, 0.0f }, Vec3d{ 0.0f, 1.0f, 0.0f });
        ring(Vec3d{ 0.0f, 1.0f, 0.0f }, Vec3d{ 0.0f, 0.0f, 1.0f });
    }

    // Preview lines draw on top: the author is usually INSIDE a zone, where
    // depth-tested bounds hide behind the room's own walls.
    if (!segments.empty())
        Lines.Submit(frame, viewport, segments, /*onTop*/ view.StreamingPreview, "ZoneBoundsRenderer");
}
