#include "ViewportPanel.h"

#include "EditorUiStyle.h"
#include "fonts/IconsFontAwesome6.h"

#include "../viewport/MarqueeState.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>

namespace
{
constexpr ImGuiWindowFlags kViewportChildFlags =
    ImGuiWindowFlags_NoMove
    | ImGuiWindowFlags_NoScrollbar
    | ImGuiWindowFlags_NoScrollWithMouse
    | ImGuiWindowFlags_NoBackground;
}

ViewportPanel::ViewportPanel(ViewportLayout& layout, const MarqueeState& marquee)
    : Layout(layout)
    , Marquee(marquee)
{
}

std::string_view ViewportPanel::GetTitle() const
{
    return "Viewport";
}

void ViewportPanel::OnDraw()
{
    // Dock-managed: the host docks this into the central node (see EditorUiFeature).
    // NoBackground keeps the window transparent so the 3D scene — drawn into the
    // swapchain behind ImGui and scissored to RegionMin/Max — shows through.
    const ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoBackground;

    // Recomputed each frame as the leaves draw; OR-ed across all viewports.
    RegionHovered = false;
    RegionRects.clear();

    if (!ImGui::Begin(GetTitle().data(), &Visible, windowFlags))
    {
        ImGui::End();
        return;
    }

    DrawLayoutToggle();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (Layout.GetMode() == LayoutMode::Single)
        DrawSingleView(avail);
    else
        DrawNode(Layout.Tree(), avail);

    FillGapsBehindViewports();

    ImGui::End();
}

void ViewportPanel::DrawLayoutToggle()
{
    const bool single = Layout.GetMode() == LayoutMode::Single;
    if (ImGui::SmallButton(single ? ICON_FA_TABLE_CELLS_LARGE : ICON_FA_WINDOW_MAXIMIZE))
    {
        Layout.SetMode(single ? LayoutMode::Quad : LayoutMode::Single);
        if (Layout.GetMode() == LayoutMode::Single)
            SyncTabToOrientation = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(single ? "Switch to four-way layout" : "Switch to single tabbed viewport");

    // In single mode the orientation tabs share this row with the toggle.
    if (single)
        ImGui::SameLine();
}

void ViewportPanel::DrawSingleView(ImVec2 size)
{
    EditorViewport* viewport = Layout.Active();
    if (viewport == nullptr && !Layout.All().empty())
        viewport = Layout.All().front().get();
    if (viewport == nullptr)
        return;

    // The tab bar both reflects and drives the single viewport's orientation. On
    // the frame we entered single mode, force-select the current orientation;
    // otherwise the active tab is authoritative so clicks switch the view.
    if (ImGui::BeginTabBar("##ViewOrientationTabs", ImGuiTabBarFlags_None))
    {
        for (ViewportOrientation orientation : AllViewportOrientations())
        {
            ImGuiTabItemFlags flags = 0;
            if (SyncTabToOrientation && viewport->Orientation == orientation)
                flags |= ImGuiTabItemFlags_SetSelected;
            if (ImGui::BeginTabItem(Traits(orientation).Label, nullptr, flags))
            {
                if (!SyncTabToOrientation && viewport->Orientation != orientation)
                    viewport->ApplyOrientation(orientation);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    SyncTabToOrientation = false;

    DrawViewport(*viewport, ImGui::GetContentRegionAvail(), /*showOrientationSelector=*/false);
}

void ViewportPanel::DrawNode(const LayoutNode& node, ImVec2 size)
{
    if (node.Kind == LayoutNode::NodeKind::Leaf)
    {
        EditorViewport* viewport = Layout.Find(node.Viewport);
        if (viewport == nullptr)
            return;

        ImGui::PushID(static_cast<int>(viewport->Id.Value));
        DrawViewport(*viewport, size);
        ImGui::PopID();
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    if (node.SplitAxis == LayoutNode::Axis::Horizontal)
    {
        const float width = std::max(0.0f, size.x - style.ItemSpacing.x);
        const float firstWidth = width * node.Ratio;
        const float secondWidth = width - firstWidth;
        DrawNode(*node.First, ImVec2(firstWidth, size.y));
        ImGui::SameLine(0.0f, style.ItemSpacing.x);
        DrawNode(*node.Second, ImVec2(secondWidth, size.y));
        return;
    }

    const float height = std::max(0.0f, size.y - style.ItemSpacing.y);
    const float firstHeight = height * node.Ratio;
    const float secondHeight = height - firstHeight;
    DrawNode(*node.First, ImVec2(size.x, firstHeight));
    DrawNode(*node.Second, ImVec2(size.x, secondHeight));
}

void ViewportPanel::DrawViewport(EditorViewport& viewport, ImVec2 size, bool showOrientationSelector)
{
    ImGui::BeginChild("ViewportLeaf", size, ImGuiChildFlags_Borders, kViewportChildFlags);

    if (showOrientationSelector)
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

    ImGui::EndChild();
    ImGui::EndChild();
}

void ViewportPanel::FillGapsBehindViewports()
{
    // The panel window is NoBackground so the 3D scene shows through the viewport
    // region rects. Everything else (splitter gaps, per-viewport header strips, the
    // toggle row) would otherwise show the engine's bright clear color. Fill that
    // complement with the dark panel color: build a grid from the region-rect edges
    // and fill each cell whose center lies outside every region. General over the
    // single/quad/arbitrary-split layouts.
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
        const bool selected = viewport.Orientation == orientation;
        if (ImGui::Selectable(Traits(orientation).Label, selected))
            viewport.ApplyOrientation(orientation);
        if (selected)
            ImGui::SetItemDefaultFocus();
    }

    ImGui::EndCombo();
}
