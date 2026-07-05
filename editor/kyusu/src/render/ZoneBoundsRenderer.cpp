#include "ZoneBoundsRenderer.h"

#include "EditorTheme.h"

#include "document/TransitionConnect.h"
#include "document/WorldDocument.h"
#include "viewport/WorldViewSettings.h"

#include <zone/ZoneDemand.h>

#include <array>
#include <cmath>
#include <vector>

namespace
{

void AppendBoxEdges(std::vector<EditorLineSegment>& segments, const Aabb3d& box,
                    const Vec4& color)
{
    const Vec3d& lo = box.Min;
    const Vec3d& hi = box.Max;
    const std::array<Vec3d, 8> corners = {
        Vec3d{ lo.X, lo.Y, lo.Z }, Vec3d{ hi.X, lo.Y, lo.Z },
        Vec3d{ hi.X, lo.Y, hi.Z }, Vec3d{ lo.X, lo.Y, hi.Z },
        Vec3d{ lo.X, hi.Y, lo.Z }, Vec3d{ hi.X, hi.Y, lo.Z },
        Vec3d{ hi.X, hi.Y, hi.Z }, Vec3d{ lo.X, hi.Y, hi.Z },
    };
    static constexpr std::array<std::pair<int, int>, 12> kEdges = { {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    } };
    for (const auto& [a, b] : kEdges)
        segments.push_back(EditorLineSegment{ corners[static_cast<size_t>(a)],
                                              corners[static_cast<size_t>(b)], color,
                                              EditorTheme::OverlayLinePixels });
}

} // namespace

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
        const std::vector<std::string> activeTags = SplitTagList(view.PreviewTags);
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
        AppendBoxEdges(segments, zone.Bounds, color);
    }

    // The proximity horizon as a ground-plane circle around the preview focus
    // position: which cells fall inside is read directly off the viewport, so
    // a mistuned radius is visible at a glance.
    if (view.StreamingPreview && previewConfig.Radius > 0.0)
    {
        constexpr int kSegments = 64;
        const double radius = previewConfig.Radius;
        const Vec3d& center = view.PreviewFocusPosition;
        Vec3d previous{ center.X + radius, center.Y, center.Z };
        for (int i = 1; i <= kSegments; ++i)
        {
            const double angle = 2.0 * 3.14159265358979323846 * i / kSegments;
            const Vec3d point{ center.X + radius * std::cos(angle), center.Y,
                               center.Z + radius * std::sin(angle) };
            segments.push_back(EditorLineSegment{ previous, point,
                                                  EditorTheme::PreviewDemanded,
                                                  EditorTheme::OverlayLinePixels });
            previous = point;
        }
    }

    // The transition graph over the bounds: one line per edge between zone
    // centers (a symmetric pair overlaps into one visual line).
    if (view.StreamingPreview)
    {
        const auto zoneCenter = [&](ZoneId zone) -> std::optional<Vec3d>
        {
            for (const ZoneHeader& header : world.Manifest().Zones)
                if (header.Id == zone && header.Bounds.IsValid())
                    return header.Bounds.Center();
            return std::nullopt;
        };
        for (const TransitionRecord& record : world.Manifest().Transitions)
        {
            const auto from = zoneCenter(record.From);
            const auto to = zoneCenter(record.To);
            if (!from.has_value() || !to.has_value())
                continue;
            segments.push_back(EditorLineSegment{ *from, *to, EditorTheme::TransitionLine,
                                                  EditorTheme::OverlayLinePixels });
        }
    }

    // Preview lines draw on top: the author is usually INSIDE a zone, where
    // depth-tested bounds hide behind the room's own walls.
    if (!segments.empty())
        Lines.Submit(frame, viewport, segments, /*onTop*/ view.StreamingPreview);
}
