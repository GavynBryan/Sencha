#include "ViewportPanel.h"

#include "ui/chrome/ChromeSelection.h"

#include "ui/chrome/ChromeControls.h"

#include "SceneBrowserPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "ui/chrome/ChromeHeader.h"

#include "EditorTheme.h"
#include "overlay/EditorOverlayState.h"
#include "viewport/EditorViewport.h"
#include "viewport/MarqueeState.h"
#include "viewport/ViewportButtonMath.h"
#include "viewport/ViewportDialPlacement.h"
#include "viewport/ViewportProjection.h"
#include "render/ViewportTargetCache.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace
{
constexpr ImGuiWindowFlags kViewportChildFlags =
    ImGuiWindowFlags_NoMove
    | ImGuiWindowFlags_NoScrollbar
    | ImGuiWindowFlags_NoScrollWithMouse;
}

ViewportPanel::ViewportPanel(ViewportLayout& layout, const MarqueeState& marquee, const EditorOverlayState& overlay,
                             ViewportTargetCache& targets, std::string title, DockSlot slot, float dockWeight,
                             ViewportId viewport)
    : Layout(layout)
    , Marquee(marquee)
    , Overlay(overlay)
    , Targets(targets)
    , Title(std::move(title))
    , Slot(slot)
    , Weight(dockWeight)
    , Viewport(viewport)
{
}

void ViewportPanel::ClearViewportRegion()
{
    RegionHovered = false;
    if (EditorViewport* viewport = Layout.Find(Viewport))
    {
        viewport->RegionMin = ImVec2(0.0f, 0.0f);
        viewport->RegionMax = ImVec2(0.0f, 0.0f);
    }
}

void ViewportPanel::OnDraw()
{
    // Dock-managed: the host docks this into its slot (see EditorUiFeature).
    // The Viewport frame is the quietest weight, so the scene keeps the area;
    // the scene arrives as an offscreen target that DrawViewport composites.
    const ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    RegionHovered = false;

    ScopedPanel panel(Title, &Visible, PanelStyle::Viewport, windowFlags);
    if (!panel.IsOpen())
    {
        // Collapsed or fully clipped: no rect was drawn this frame, so drop the
        // stale one; input must not route to a view that is not on screen.
        ClearViewportRegion();
        return;
    }

    if (EditorViewport* viewport = Layout.Find(Viewport))
        DrawViewport(*viewport, ImGui::GetContentRegionAvail());
}

void ViewportPanel::DrawViewport(EditorViewport& viewport, ImVec2 size)
{
    ImGui::BeginChild("ViewportLeaf", size, ImGuiChildFlags_None, kViewportChildFlags);

    DrawOrientationSelector(viewport);

    const ImVec2 renderSize(
        std::max(0.0f, ImGui::GetContentRegionAvail().x),
        std::max(0.0f, ImGui::GetContentRegionAvail().y));
    ImGui::BeginChild("ViewportRegion", renderSize, ImGuiChildFlags_None, kViewportChildFlags);

    // This child holds only the 3D render area (the orientation combo lives in the
    // parent child), so hovering it means the cursor is over the scene with no
    // panel on top — the passthrough region where input belongs to the tools.
    if (ImGui::IsWindowHovered())
        RegionHovered = true;

    viewport.RegionMin = ImGui::GetWindowPos();
    viewport.RegionMax = ImVec2(viewport.RegionMin.x + ImGui::GetWindowSize().x,
                                viewport.RegionMin.y + ImGui::GetWindowSize().y);

    // Composite this viewport's offscreen render (filled by the Offscreen phase this
    // frame). Recording the pixel size here also drives the target size next render.
    const VkExtent2D targetExtent{
        static_cast<uint32_t>(std::max(0.0f, renderSize.x)),
        static_cast<uint32_t>(std::max(0.0f, renderSize.y)),
    };
    if (const ImTextureID tex = Targets.Display(viewport.Id, targetExtent))
    {
        // Snap to integer pixels and display at the texture's exact integer size so the
        // nearest-sampled copy maps 1:1; a fractional position would resample the texels
        // against the pixel grid.
        ImGui::SetCursorScreenPos(ImVec2(std::round(viewport.RegionMin.x),
                                         std::round(viewport.RegionMin.y)));
        ImGui::Image(tex, ImVec2(static_cast<float>(targetExtent.width),
                                 static_cast<float>(targetExtent.height)));
        if (SceneDrop && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(SceneBrowserPanel::kDragPayloadType))
            {
                SceneDrop(viewport.Id, ImGui::GetMousePos(),
                          std::string_view(static_cast<const char*>(payload->Data),
                                           static_cast<std::size_t>(payload->DataSize) - 1));
            }
            ImGui::EndDragDropTarget();
        }
    }

    // The active view is the one being edited, so it carries the selection
    // outline; the others keep the steel hairline.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (viewport.IsActive)
        EditorChrome::SelectionOutline(drawList, viewport.RegionMin, viewport.RegionMax);
    else
        drawList->AddRect(viewport.RegionMin, viewport.RegionMax, ImGui::GetColorU32(EditorUi::Border));

    // Rubber-band selection rectangle, drawn in the viewport it was started in.
    if (Marquee.Active && Marquee.Viewport == viewport.Id)
    {
        const ImVec2 lo(std::min(Marquee.Start.x, Marquee.Current.x),
                        std::min(Marquee.Start.y, Marquee.Current.y));
        const ImVec2 hi(std::max(Marquee.Start.x, Marquee.Current.x),
                        std::max(Marquee.Start.y, Marquee.Current.y));
        drawList->AddRectFilled(lo, hi, ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::Accent, 0.16f)));
        drawList->AddRect(lo, hi, ImGui::GetColorU32(EditorUi::AccentHover));
    }

    DrawOverlay(viewport, drawList);

    ImGui::EndChild();
    ImGui::EndChild();
}

void ViewportPanel::DrawOverlay(const EditorViewport& viewport, ImDrawList* drawList)
{
    const ViewportProjection projection(viewport);
    const auto inRegion = [&](ImVec2 px) {
        return px.x >= viewport.RegionMin.x && px.x <= viewport.RegionMax.x
            && px.y >= viewport.RegionMin.y && px.y <= viewport.RegionMax.y;
    };
    const auto toColor = [](const Vec4& c) {
        return ImGui::GetColorU32(ImVec4(c.X, c.Y, c.Z, c.W));
    };

    // An ortho view looks down one world axis; a dimension along that axis is
    // perpendicular to the screen and can't be read, so hide its label here.
    int hiddenAxis = -1;
    if (viewport.Camera.ActiveMode == EditorCamera::Mode::Orthographic)
    {
        const Vec3d n = viewport.Camera.OrthoAxis;
        const float ax = std::abs(n.X);
        const float ay = std::abs(n.Y);
        const float az = std::abs(n.Z);
        hiddenAxis = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
    }

    // World-anchored dimension labels for the current selection, shown in every
    // viewport (each view projects the same world points).
    for (const LabelRequest& label : Overlay.Labels)
    {
        if (hiddenAxis >= 0 && label.Axis == hiddenAxis)
            continue;
        const std::optional<ProjectedPoint> p = projection.WorldToPixel(label.World);
        if (!p.has_value() || !inRegion(p->Pixel))
            continue;
        drawList->AddText(ImVec2(p->Pixel.x + 4.0f, p->Pixel.y - 6.0f), toColor(label.Color), label.Text.c_str());
    }

    for (const PointHandleRequest& handle : Overlay.PointHandles)
    {
        if (handle.Viewport.IsValid() && handle.Viewport != viewport.Id)
            continue;
        const std::optional<ProjectedPoint> p = projection.WorldToPixel(handle.World);
        if (!p.has_value() || !inRegion(p->Pixel))
            continue;
        const float half = std::max(2.0f, handle.SizePixels * 0.5f);
        const ImVec2 min(p->Pixel.x - half, p->Pixel.y - half);
        const ImVec2 max(p->Pixel.x + half, p->Pixel.y + half);
        drawList->AddRectFilled(min, max, toColor(handle.Fill), 1.0f);
        drawList->AddRect(min, max, toColor(handle.Border), 1.0f, 0, 1.5f);
    }
    // A tool's rotation dial, lying in the plane it turns things on. Skipped
    // whole when any part of the ring is behind the camera: half a projected
    // circle would draw as a line across the view and hit-test as one too.
    for (const ViewportDialRequest& request : Overlay.ViewportDials)
    {
        if (request.Viewport.IsValid() && request.Viewport != viewport.Id)
            continue;
        const ViewportDial::Placement placement = ViewportDial::PlaceIn(
            viewport, request.Center, request.AxisU, request.AxisV, request.BoxSemiMinor);
        if (!placement.Visible)
            continue;
        std::array<Vec3d, ViewportDial::kRimSegments + 1> ring{};
        const int count = ViewportDial::RimPoints(placement, ring);

        std::vector<ImVec2> rim;
        rim.reserve(static_cast<std::size_t>(count));
        bool whole = true;
        for (int i = 0; i < count && whole; ++i)
        {
            const std::optional<ProjectedPoint> p =
                projection.WorldToPixel(ring[static_cast<std::size_t>(i)]);
            whole = p.has_value();
            if (whole)
                rim.push_back(p->Pixel);
        }
        const std::optional<ProjectedPoint> knob =
            projection.WorldToPixel(placement.PointAt(request.Angle));
        if (!whole || !knob.has_value())
            continue;

        std::array<Vec3d, 64> stops{};
        const int tickCount = ViewportDial::TickPoints(placement, request.TickIncrement, stops);
        std::vector<ImVec2> ticks;
        ticks.reserve(static_cast<std::size_t>(tickCount));
        for (int i = 0; i < tickCount; ++i)
        {
            const std::optional<ProjectedPoint> p =
                projection.WorldToPixel(stops[static_cast<std::size_t>(i)]);
            if (p.has_value())
                ticks.push_back(p->Pixel);
        }

        EditorChrome::DrawDial(drawList, rim, ticks, knob->Pixel, request.Hot);
    }

    // Tool buttons pinned over the geometry they act on. Painted here rather
    // than made into ImGui items so the viewport's own input keeps working
    // underneath them; the tool that asked for them tests the same rects.
    for (const ViewportButtonRequest& request : Overlay.ViewportButtons)
    {
        if (request.Viewport.IsValid() && request.Viewport != viewport.Id)
            continue;
        std::vector<std::optional<ImVec2>> anchors;
        anchors.reserve(request.Anchors.size());
        for (const Vec3d& world : request.Anchors)
        {
            const std::optional<ProjectedPoint> p = projection.WorldToPixel(world);
            anchors.push_back(p.has_value() ? std::optional<ImVec2>(p->Pixel) : std::nullopt);
        }
        const ViewportButtons::Row row =
            ViewportButtons::Layout(anchors, static_cast<int>(request.Buttons.size()),
                                    EditorUi::Px(1.0f), viewport.RegionMin, viewport.RegionMax);
        if (!row.Visible)
            continue;

        for (int i = 0; i < row.Count; ++i)
        {
            const ViewportButton& button = request.Buttons[static_cast<std::size_t>(i)];
            const ImVec2 min = row.MinOf(i);
            const ImVec2 max = row.MaxOf(i);
            const bool hot = request.Hot == i;
            if (button.Icon == IconId::None)
                EditorChrome::DrawTextButton(drawList, min, max, button.Label.c_str(), button.Tone,
                                             button.Enabled, hot);
            else
                EditorChrome::DrawIconButton(drawList, min, max, button.Icon, button.Tone, button.Enabled,
                                             hot);
        }

        // The caption is a readout the row places, not a control: centred over
        // the buttons it describes and never hit-tested.
        if (request.Caption.has_value())
        {
            const ImVec2 center = row.CaptionCenter(request.Caption->FirstButton,
                                                    request.Caption->LastButton);
            const ImVec2 size = ImGui::CalcTextSize(request.Caption->Text.c_str());
            drawList->AddText(ImVec2(center.x - size.x * 0.5f, center.y - size.y),
                              toColor(EditorTheme::DimensionLabel), request.Caption->Text.c_str());
        }
    }


    // Hovered edge's length, anchored at its midpoint.
    if (!Overlay.Hover.Measure.empty())
    {
        const std::optional<ProjectedPoint> p = projection.WorldToPixel(Overlay.Hover.MeasureAnchor);
        if (p.has_value() && inRegion(p->Pixel))
            drawList->AddText(ImVec2(p->Pixel.x + 4.0f, p->Pixel.y - 6.0f),
                              toColor(EditorTheme::HoverEligible), Overlay.Hover.Measure.c_str());
    }
    // Construction lines a tool laid in the world, in every view that can see them.
    for (const WorldSegmentRequest& segment : Overlay.Segments)
    {
        if (segment.Viewport.IsValid() && segment.Viewport != viewport.Id)
            continue;
        const std::optional<ProjectedPoint> a = projection.WorldToPixel(segment.From);
        const std::optional<ProjectedPoint> b = projection.WorldToPixel(segment.To);
        if (a.has_value() && b.has_value())
            drawList->AddLine(a->Pixel, b->Pixel, toColor(segment.Color), segment.Thickness);
    }


    // Active drag's origin->current line + distance, only in the view it started in.
    if (Overlay.Readout.Active() && Overlay.Readout.Viewport == viewport.Id)
    {
        const std::optional<ProjectedPoint> a = projection.WorldToPixel(*Overlay.Readout.From);
        const std::optional<ProjectedPoint> b = projection.WorldToPixel(*Overlay.Readout.To);
        if (a.has_value() && b.has_value())
        {
            const ImU32 color = toColor(EditorTheme::Readout);
            drawList->AddLine(a->Pixel, b->Pixel, color, 2.0f);
            const ImVec2 mid((a->Pixel.x + b->Pixel.x) * 0.5f, (a->Pixel.y + b->Pixel.y) * 0.5f);
            drawList->AddText(ImVec2(mid.x + 4.0f, mid.y - 6.0f), color, Overlay.Readout.Text.c_str());
        }
    }
}

void ViewportPanel::DrawOrientationSelector(EditorViewport& viewport)
{
    // The view's header row: the perspective view names itself; the ortho view
    // names the panel and keeps its orientation combo in the control region,
    // so the row costs no more height than the combo did.
    const bool perspective = viewport.Orientation == ViewportOrientation::Perspective;
    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    const float rowHeight = std::max(EditorUi::Px(EditorUi::Metrics.HeaderHeight), ImGui::GetFrameHeight() + EditorUi::Px(4.0f));
    const ImVec2 rowMax(rowMin.x + std::max(0.0f, ImGui::GetContentRegionAvail().x), rowMin.y + rowHeight);
    const float controlWidth = perspective ? 0.0f : ImGui::GetFontSize() * 7.0f;
    const EditorChrome::HeaderRegions regions = EditorChrome::DrawHeaderRow(
        ImGui::GetWindowDrawList(), rowMin, rowMax, perspective ? viewport.GetDisplayLabel() : Title,
        EditorUi::TextRole::PanelTitle, EditorChrome::HeaderState{ .Focused = viewport.IsActive }, controlWidth);

    if (!perspective && regions.HasControl)
    {
        ImGui::SetCursorScreenPos(regions.ControlMin);
        ImGui::SetNextItemWidth(regions.ControlMax.x - regions.ControlMin.x);
        if (EditorChrome::BeginCombo("##Orientation", viewport.GetDisplayLabel()))
        {
            for (ViewportOrientation orientation : AllViewportOrientations())
            {
                // The ortho view stays orthographic: only the fixed ortho orientations
                // are offered (no Perspective, no camera-axis User view).
                const OrientationTraits& traits = Traits(orientation);
                if (traits.Mode != EditorCamera::Mode::Orthographic || traits.UsesCameraAxis)
                    continue;
                const bool selected = viewport.Orientation == orientation;
                if (ImGui::Selectable(traits.Label, selected))
                    viewport.ApplyOrientation(orientation);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            EditorChrome::EndCombo();
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMax.y + EditorUi::Px(2.0f)));
}
