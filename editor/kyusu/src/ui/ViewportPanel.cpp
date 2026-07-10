#include "ViewportPanel.h"

#include "ui/EditorUiStyle.h"

#include "EditorTheme.h"
#include "overlay/EditorOverlayState.h"
#include "viewport/EditorViewport.h"
#include "viewport/MarqueeState.h"
#include "viewport/ViewportProjection.h"
#include "render/ViewportTargetCache.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <optional>

namespace
{
constexpr ImGuiWindowFlags kViewportChildFlags =
    ImGuiWindowFlags_NoMove
    | ImGuiWindowFlags_NoScrollbar
    | ImGuiWindowFlags_NoScrollWithMouse
    | ImGuiWindowFlags_NoBackground;
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
    // NoBackground keeps the window transparent so the 3D scene — drawn into the
    // swapchain behind ImGui and scissored to RegionMin/Max — shows through.
    const ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoBackground;

    RegionHovered = false;
    RegionRects.clear();

    if (!ImGui::Begin(Title.c_str(), &Visible, windowFlags))
    {
        // Collapsed or fully clipped: no rect was drawn this frame, so drop the
        // stale one; input must not route to a view that is not on screen.
        ClearViewportRegion();
        ImGui::End();
        return;
    }

    if (EditorViewport* viewport = Layout.Find(Viewport))
        DrawViewport(*viewport, ImGui::GetContentRegionAvail());

    FillGapsBehindViewports();

    ImGui::End();
}

void ViewportPanel::DrawViewport(EditorViewport& viewport, ImVec2 size)
{
    ImGui::BeginChild("ViewportLeaf", size, ImGuiChildFlags_Borders, kViewportChildFlags);

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
    RegionRects.emplace_back(viewport.RegionMin, viewport.RegionMax);

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
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 borderColor = viewport.IsActive
        ? ImGui::GetColorU32(EditorUi::Accent)
        : ImGui::GetColorU32(EditorUi::Border);
    drawList->AddRect(viewport.RegionMin, viewport.RegionMax, borderColor);

    // Rubber-band selection rectangle, drawn in the viewport it was started in.
    if (Marquee.Active && Marquee.Viewport == viewport.Id)
    {
        const ImVec2 lo(std::min(Marquee.Start.x, Marquee.Current.x),
                        std::min(Marquee.Start.y, Marquee.Current.y));
        const ImVec2 hi(std::max(Marquee.Start.x, Marquee.Current.x),
                        std::max(Marquee.Start.y, Marquee.Current.y));
        drawList->AddRectFilled(lo, hi, ImGui::GetColorU32(ImVec4(EditorUi::Accent.x, EditorUi::Accent.y, EditorUi::Accent.z, 0.16f)));
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

    // Hovered edge's length, anchored at its midpoint.
    if (!Overlay.Hover.Measure.empty())
    {
        const std::optional<ProjectedPoint> p = projection.WorldToPixel(Overlay.Hover.MeasureAnchor);
        if (p.has_value() && inRegion(p->Pixel))
            drawList->AddText(ImVec2(p->Pixel.x + 4.0f, p->Pixel.y - 6.0f),
                              toColor(EditorTheme::HoverEligible), Overlay.Hover.Measure.c_str());
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

void ViewportPanel::FillGapsBehindViewports()
{
    // The panel window is NoBackground so the 3D scene shows through the viewport
    // region rect. Everything else (the header strip, border gaps) would otherwise
    // show the engine's bright clear color. Fill that complement with the dark
    // panel color: build a grid from the region-rect edges and fill each cell
    // whose center lies outside every region.
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 cMin(wp.x + ImGui::GetWindowContentRegionMin().x,
                      wp.y + ImGui::GetWindowContentRegionMin().y);
    const ImVec2 cMax(wp.x + ImGui::GetWindowContentRegionMax().x,
                      wp.y + ImGui::GetWindowContentRegionMax().y);
    if (cMax.x <= cMin.x || cMax.y <= cMin.y)
        return;

    std::vector<float> xs{ cMin.x, cMax.x };
    std::vector<float> ys{ cMin.y, cMax.y };
    for (const auto& r : RegionRects)
    {
        xs.push_back(std::clamp(r.first.x, cMin.x, cMax.x));
        xs.push_back(std::clamp(r.second.x, cMin.x, cMax.x));
        ys.push_back(std::clamp(r.first.y, cMin.y, cMax.y));
        ys.push_back(std::clamp(r.second.y, cMin.y, cMax.y));
    }
    const auto dedup = [](std::vector<float>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end(),
                            [](float a, float b) { return std::abs(a - b) < 0.5f; }),
                v.end());
    };
    dedup(xs);
    dedup(ys);

    const auto insideRegion = [&](float px, float py) {
        for (const auto& r : RegionRects)
            if (px >= r.first.x && px <= r.second.x && py >= r.first.y && py <= r.second.y)
                return true;
        return false;
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 fill = ImGui::GetColorU32(EditorUi::PanelBg);
    for (std::size_t i = 0; i + 1 < xs.size(); ++i)
        for (std::size_t j = 0; j + 1 < ys.size(); ++j)
        {
            const float cx = (xs[i] + xs[i + 1]) * 0.5f;
            const float cy = (ys[j] + ys[j + 1]) * 0.5f;
            if (!insideRegion(cx, cy))
                dl->AddRectFilled(ImVec2(xs[i], ys[j]), ImVec2(xs[i + 1], ys[j + 1]), fill);
        }
}

void ViewportPanel::DrawOrientationSelector(EditorViewport& viewport)
{
    if (viewport.Orientation == ViewportOrientation::Perspective)
    {
        ImGui::TextUnformatted(viewport.GetDisplayLabel());
        return;
    }

    const char* preview = viewport.GetDisplayLabel();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!ImGui::BeginCombo("##Orientation", preview))
        return;

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

    ImGui::EndCombo();
}
