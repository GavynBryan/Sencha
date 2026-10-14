#include "overlay/ViewMarks.h"

#include "EditorTheme.h"
#include "overlay/EditorOverlayState.h"
#include "viewport/EditorViewport.h"
#include "viewport/MarqueeState.h"
#include "viewport/ViewportButtonMath.h"
#include "viewport/ViewportDialPlacement.h"
#include "viewport/ViewportProjection.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
// Where a world label's text starts, relative to the point it names.
const Vec2d kLabelOffset(4.0f, -6.0f);
constexpr float kKnobPixels = 8.0f;
// A caption or tooltip line, centred on a point: wide enough for a sentence.
constexpr float kLineWidth = 320.0f;
constexpr float kLineHeight = 16.0f;

// The line of text centred on `centre`, its bottom at `bottom`.
Rect2d LineAbove(float centre, float bottom, float scale)
{
    return Rect2d(centre - kLineWidth * scale * 0.5f, bottom - kLineHeight * scale, kLineWidth * scale,
                  kLineHeight * scale);
}

bool ShownIn(ViewportId limit, const EditorViewport& viewport)
{
    return !limit.IsValid() || limit == viewport.Id;
}

// The world axis an ortho view looks down; a dimension along it cannot be read.
int HiddenAxis(const EditorViewport& viewport)
{
    if (viewport.Camera.ActiveMode != EditorCamera::Mode::Orthographic)
        return -1;
    const Vec3d n = viewport.Camera.OrthoAxis;
    const float ax = std::abs(n.X);
    const float ay = std::abs(n.Y);
    const float az = std::abs(n.Z);
    return (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
}

Rect2d Centred(Vec2d at, float size)
{
    return Rect2d(at.X - size * 0.5f, at.Y - size * 0.5f, size, size);
}
} // namespace

void AppendViewMarks(const EditorOverlayState& overlay, const MarqueeState& marquee, const EditorViewport& viewport,
                     float scale, std::vector<ViewMark>& out)
{
    const ViewportProjection projection(viewport);
    const auto inRegion = [&](Vec2d px) {
        return px.X >= viewport.RegionMin.X && px.X <= viewport.RegionMax.X && px.Y >= viewport.RegionMin.Y
            && px.Y <= viewport.RegionMax.Y;
    };
    const auto label = [&](Vec3d world, std::string text, Vec4 color) {
        const std::optional<ProjectedPoint> p = projection.WorldToPixel(world);
        if (p.has_value() && inRegion(p->Pixel))
            out.push_back({ .Kind = ViewMarkKind::Label, .Rect = Rect2d(p->Pixel + kLabelOffset, Vec2d{}),
                            .Text = std::move(text), .Color = color });
    };

    const int hiddenAxis = HiddenAxis(viewport);
    for (const LabelRequest& request : overlay.Labels)
        if (hiddenAxis < 0 || request.Axis != hiddenAxis)
            label(request.World, request.Text, request.Color);
    for (const LabelRequest& request : overlay.Handles.Values)
        label(request.World, request.Text, request.Color);

    if (!overlay.Hover.Measure.empty())
        label(overlay.Hover.MeasureAnchor, overlay.Hover.Measure, EditorTheme::HoverEligible);

    if (overlay.Readout.Active() && overlay.Readout.Viewport == viewport.Id)
    {
        const std::optional<ProjectedPoint> a = projection.WorldToPixel(*overlay.Readout.From);
        const std::optional<ProjectedPoint> b = projection.WorldToPixel(*overlay.Readout.To);
        if (a.has_value() && b.has_value())
            out.push_back({ .Kind = ViewMarkKind::Label,
                            .Rect = Rect2d((a->Pixel + b->Pixel) * 0.5f + kLabelOffset, Vec2d{}),
                            .Text = overlay.Readout.Text,
                            .Color = EditorTheme::Readout });
    }

    for (const PointHandleRequest& handle : overlay.PointHandles)
    {
        if (!ShownIn(handle.Viewport, viewport))
            continue;
        const std::optional<ProjectedPoint> p = projection.WorldToPixel(handle.World);
        if (p.has_value() && inRegion(p->Pixel))
            out.push_back({ .Kind = ViewMarkKind::Handle,
                            .Rect = Centred(p->Pixel, std::max(4.0f, handle.SizePixels)),
                            .Color = handle.Fill,
                            .Border = handle.Border });
    }

    for (const PropertyKnobRequest& knob : overlay.Handles.Knobs)
    {
        if (!ShownIn(knob.Viewport, viewport))
            continue;
        if (const std::optional<ProjectedPoint> p = projection.WorldToPixel(knob.World); p.has_value() && inRegion(p->Pixel))
            out.push_back({ .Kind = ViewMarkKind::Knob,
                            .Rect = Centred(p->Pixel, kKnobPixels * scale * (knob.Hot ? 1.5f : 0.75f)),
                            .Hot = knob.Hot });
    }

    for (const ViewportDialRequest& request : overlay.ViewportDials)
    {
        if (!ShownIn(request.Viewport, viewport))
            continue;
        const ViewportDial::Placement placement =
            ViewportDial::PlaceIn(viewport, request.Center, request.AxisU, request.AxisV, request.BoxSemiMinor);
        if (!placement.Visible)
            continue;
        if (const std::optional<ProjectedPoint> knob = projection.WorldToPixel(placement.PointAt(request.Angle)))
            out.push_back({ .Kind = ViewMarkKind::Knob, .Rect = Centred(knob->Pixel, kKnobPixels * scale),
                            .Hot = request.Hot });
    }

    for (const ViewportButtonRequest& request : overlay.ViewportButtons)
    {
        if (!ShownIn(request.Viewport, viewport))
            continue;
        std::vector<std::optional<Vec2d>> anchors;
        anchors.reserve(request.Anchors.size());
        for (const Vec3d& world : request.Anchors)
        {
            const std::optional<ProjectedPoint> p = projection.WorldToPixel(world);
            anchors.push_back(p.has_value() ? std::optional<Vec2d>(p->Pixel) : std::nullopt);
        }
        const ViewportButtons::Row row = ViewportButtons::Layout(
            anchors, static_cast<int>(request.Buttons.size()), scale, viewport.RegionMin, viewport.RegionMax);
        if (!row.Visible)
            continue;
        for (int i = 0; i < row.Count; ++i)
        {
            const ViewportButton& button = request.Buttons[static_cast<std::size_t>(i)];
            const Rect2d rect = Rect2d::FromMinMax(row.MinOf(i), row.MaxOf(i));
            const bool hot = request.Hot == i;
            out.push_back({ .Kind = ViewMarkKind::Button,
                            .Rect = rect,
                            .Text = button.Icon == IconId::None ? button.Label : std::string{},
                            .Icon = button.Icon,
                            .Tone = button.Tone,
                            .Hot = hot,
                            .Enabled = button.Enabled });
            if (hot && !button.Tooltip.empty())
                out.push_back({ .Kind = ViewMarkKind::Tooltip,
                                .Rect = LineAbove(rect.Center().X, rect.Max().Y + (kLineHeight + 6.0f) * scale, scale),
                                .Text = button.Tooltip });
        }
        if (request.Caption.has_value())
        {
            const Vec2d centre = row.CaptionCenter(request.Caption->FirstButton, request.Caption->LastButton);
            out.push_back({ .Kind = ViewMarkKind::Caption,
                            .Rect = LineAbove(centre.X, centre.Y, scale),
                            .Text = request.Caption->Text,
                            .Color = EditorTheme::DimensionLabel });
        }
    }

    for (const MarkerRequest& marker : overlay.Markers)
    {
        const std::optional<ProjectedPoint> p = projection.WorldToPixel(marker.World);
        if (p.has_value() && inRegion(p->Pixel))
            out.push_back({ .Kind = ViewMarkKind::Marker,
                            .Rect = Centred(p->Pixel, EditorTheme::MarkerPixels),
                            .Icon = marker.Icon,
                            .Hot = marker.Selected });
    }

    if (marquee.Active && marquee.Viewport == viewport.Id)
    {
        const Vec2d lo(std::min(marquee.Start.X, marquee.Current.X), std::min(marquee.Start.Y, marquee.Current.Y));
        const Vec2d hi(std::max(marquee.Start.X, marquee.Current.X), std::max(marquee.Start.Y, marquee.Current.Y));
        out.push_back({ .Kind = ViewMarkKind::Marquee, .Rect = Rect2d::FromMinMax(lo, hi) });
    }
}
