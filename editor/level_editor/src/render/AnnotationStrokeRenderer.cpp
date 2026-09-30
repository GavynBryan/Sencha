#include "AnnotationStrokeRenderer.h"

#include "EditorTheme.h"
#include "overlay/EditorOverlayState.h"
#include "viewport/ViewportDialPlacement.h"

#include <array>

namespace
{
// A dial stop is a notch across the rim, this fraction of the radius long.
constexpr float kStopLength = 0.08f;
// The drag readout's line is heavier than a construction line.
constexpr float kReadoutWidthPx = 2.0f;

bool ShownIn(ViewportId limit, const EditorViewport& viewport)
{
    return !limit.IsValid() || limit == viewport.Id;
}
} // namespace

void AnnotationStrokeRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                                            const CameraRenderData& camera)
{
    Strokes.clear();
    for (const WorldSegmentRequest& segment : Overlay.Segments)
        if (ShownIn(segment.Viewport, viewport))
            Strokes.push_back({ segment.From, segment.To, segment.Color, segment.Thickness });

    if (Overlay.Readout.Active() && Overlay.Readout.Viewport == viewport.Id)
        Strokes.push_back({ *Overlay.Readout.From, *Overlay.Readout.To, EditorTheme::Readout, kReadoutWidthPx });

    for (const ViewportDialRequest& request : Overlay.ViewportDials)
    {
        if (!ShownIn(request.Viewport, viewport))
            continue;
        const ViewportDial::Placement placement =
            ViewportDial::PlaceIn(viewport, request.Center, request.AxisU, request.AxisV, request.BoxSemiMinor);
        if (!placement.Visible)
            continue;
        std::array<Vec3d, ViewportDial::kRimSegments + 1> rim{};
        const int count = ViewportDial::RimPoints(placement, rim);
        const Vec4 rimColor = request.Hot ? EditorTheme::DialRimHot : EditorTheme::DialRim;
        for (int i = 1; i < count; ++i)
            Strokes.push_back({ rim[static_cast<std::size_t>(i - 1)], rim[static_cast<std::size_t>(i)], rimColor, 1.0f });

        std::array<Vec3d, 64> stops{};
        const int stopCount = ViewportDial::TickPoints(placement, request.TickIncrement, stops);
        for (int i = 0; i < stopCount; ++i)
        {
            const Vec3d& stop = stops[static_cast<std::size_t>(i)];
            Strokes.push_back({ stop, stop + (placement.Center - stop) * kStopLength, EditorTheme::DialStop, 1.5f });
        }
    }

    if (!Strokes.empty())
        Lines.Submit(frame, viewport, camera, Strokes, /*onTop*/ true, "annotation_strokes");
}
