#include "PreviewPanel.h"

#include "icons/IconId.h"
#include "ui/EditorUiStyle.h"
#include "ui/ScopedPanel.h"
#include "ui/chrome/ChromeBars.h"
#include "ui/chrome/ChromeControls.h"

#include <ui/UiSurfacePlacement.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace
{
    struct ResolutionPreset
    {
        const char* Label;
        RenderExtent Size;
    };
    constexpr std::array<ResolutionPreset, 5> kResolutions{ {
        { "1280 x 720", { 1280, 720 } },
        { "1920 x 1080", { 1920, 1080 } },
        { "2560 x 1440", { 2560, 1440 } },
        { "3840 x 2160", { 3840, 2160 } },
        { "1280 x 800  (handheld)", { 1280, 800 } },
    } };
    constexpr std::array<float, 4> kScales{ 1.0f, 1.25f, 1.5f, 2.0f };

    const char* ZoomLabel(PreviewViewState::Zoom zoom)
    {
        switch (zoom)
        {
        case PreviewViewState::Zoom::Fit: return "Fit";
        case PreviewViewState::Zoom::Half: return "50%";
        case PreviewViewState::Zoom::Actual: return "100%";
        case PreviewViewState::Zoom::Double: return "200%";
        }
        return "?";
    }

    // The image's size for a zoom, in window points. Fit fills the region
    // either way; the fixed zooms are the surface's pixels at that ratio.
    ImVec2 ImageSize(PreviewViewState::Zoom zoom, RenderExtent surface, ImVec2 avail)
    {
        const float w = static_cast<float>(surface.Width);
        const float h = static_cast<float>(surface.Height);
        switch (zoom)
        {
        case PreviewViewState::Zoom::Fit:
        {
            const float s = std::min(avail.x / w, avail.y / h);
            return { w * s, h * s };
        }
        case PreviewViewState::Zoom::Half: return { w * 0.5f, h * 0.5f };
        case PreviewViewState::Zoom::Actual: return { w, h };
        case PreviewViewState::Zoom::Double: return { w * 2.0f, h * 2.0f };
        }
        return { w, h };
    }

    ImVec2 ToImVec(Vec2d v) { return { v.X, v.Y }; }
}

PreviewPanel::PreviewPanel(UiPreviewSession& session,
                           PreviewViewState& view,
                           UiSurfaceTargetRenderFeature& target,
                           UiSurfaceTargetId binding)
    : Session(session)
    , View(view)
    , Target(target)
    , Binding(binding)
{
}

void PreviewPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible, PanelStyle::ViewportPrimary);
    if (!panel.IsOpen())
    {
        // A hidden preview has no placement; nothing should map into it.
        Session.SetPlacement(std::nullopt);
        return;
    }
    DrawControls();
    DrawImage();
}

void PreviewPanel::DrawControls()
{
    DrawResolutionControls();
    ImGui::SameLine();
    DrawScaleControls();
    ImGui::SameLine();
    DrawZoomControls();
    ImGui::SameLine();
    EditorChrome::Divider();
    ImGui::SameLine();

    const float size = EditorChrome::BarButtonSize();
    const bool inspecting = Session.Mode() == UiPreviewSession::PointerMode::Inspect;
    if (EditorChrome::ToolButton("inspect", IconId::Eye,
                                 "Inspect: hover measures, click selects; the document sees no input (Ctrl+I)",
                                 inspecting, size))
    {
        Session.SetPointerMode(inspecting ? UiPreviewSession::PointerMode::Interact
                                          : UiPreviewSession::PointerMode::Inspect);
        if (!inspecting)
            View.Hovered = {};
    }
    ImGui::SameLine();
    if (EditorChrome::ToolButton("safe", IconId::GridFrame, "Safe area: 5% action-safe and 10% title-safe",
                                 View.SafeArea, size))
        View.SafeArea = !View.SafeArea;
    ImGui::SameLine();
    if (EditorChrome::ToolButton("theme", "Theme",
                                 "Publish the editor's theme as theme.rcss. Only a document that links it changes",
                                 View.EditorTheme, size))
        View.EditorTheme = !View.EditorTheme;
    ImGui::SameLine();
    EditorChrome::Divider();
    ImGui::SameLine();
    DrawNavigation();
}

void PreviewPanel::DrawResolutionControls()
{
    const RenderExtent current = Session.Resolution();
    char label[48];
    std::snprintf(label, sizeof(label), "%u x %u", current.Width, current.Height);
    ImGui::SetNextItemWidth(EditorUi::Px(190.0f));
    if (EditorChrome::BeginCombo("##resolution", label))
    {
        for (const ResolutionPreset& preset : kResolutions)
        {
            const bool selected = preset.Size.Width == current.Width && preset.Size.Height == current.Height;
            if (ImGui::Selectable(preset.Label, selected))
                Session.SetResolution(preset.Size);
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(EditorUi::Px(70.0f));
        ImGui::InputInt("##cw", &CustomWidth, 0);
        ImGui::SameLine();
        ImGui::TextUnformatted("x");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(EditorUi::Px(70.0f));
        ImGui::InputInt("##ch", &CustomHeight, 0);
        ImGui::SameLine();
        if (ImGui::SmallButton("Apply") && CustomWidth > 0 && CustomHeight > 0)
        {
            Session.SetResolution({ static_cast<std::uint32_t>(CustomWidth),
                                    static_cast<std::uint32_t>(CustomHeight) });
        }
        EditorChrome::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Surface size in pixels: what the document is laid out and rendered at");
}

void PreviewPanel::DrawScaleControls()
{
    char label[24];
    std::snprintf(label, sizeof(label), "@%.2g", Session.DisplayScale());
    ImGui::SetNextItemWidth(EditorUi::Px(80.0f));
    if (EditorChrome::BeginCombo("##scale", label))
    {
        for (const float scale : kScales)
        {
            char item[16];
            std::snprintf(item, sizeof(item), "%.2g", scale);
            if (ImGui::Selectable(item, scale == Session.DisplayScale()))
                Session.SetDisplayScale(scale);
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(EditorUi::Px(70.0f));
        ImGui::InputFloat("##cs", &CustomScale, 0.0f, 0.0f, "%.2f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Apply") && CustomScale > 0.0f)
            Session.SetDisplayScale(CustomScale);
        EditorChrome::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Display scale: how many surface pixels one dp is");
}

void PreviewPanel::DrawZoomControls()
{
    ImGui::SetNextItemWidth(EditorUi::Px(80.0f));
    if (EditorChrome::BeginCombo("##zoom", ZoomLabel(View.ZoomLevel)))
    {
        for (const PreviewViewState::Zoom zoom : { PreviewViewState::Zoom::Fit, PreviewViewState::Zoom::Half,
                                                   PreviewViewState::Zoom::Actual, PreviewViewState::Zoom::Double })
        {
            if (ImGui::Selectable(ZoomLabel(zoom), zoom == View.ZoomLevel))
                View.ZoomLevel = zoom;
        }
        EditorChrome::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How the surface is shown here. 100%% is exact; Fit resamples");
}

void PreviewPanel::DrawNavigation()
{
    struct Step
    {
        const char* Id;
        const char* Label;
        const char* Tip;
        UiNavigation Direction;
    };
    constexpr std::array<Step, 6> kSteps{ {
        { "nav_up", "Up", "Move focus up (d-pad up)", UiNavigation::Up },
        { "nav_down", "Dn", "Move focus down (d-pad down)", UiNavigation::Down },
        { "nav_left", "Lt", "Move focus left (d-pad left)", UiNavigation::Left },
        { "nav_right", "Rt", "Move focus right (d-pad right)", UiNavigation::Right },
        { "nav_accept", "OK", "Activate what has focus (Accept)", UiNavigation::Accept },
        { "nav_back", "Esc", "Back out (Cancel), what a shell's Escape does", UiNavigation::Cancel },
    } };
    const float size = EditorChrome::BarButtonSize();
    ImGui::BeginDisabled(!Session.IsOpen());
    for (std::size_t i = 0; i < kSteps.size(); ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        if (EditorChrome::ToolButton(kSteps[i].Id, kSteps[i].Label, kSteps[i].Tip, false, size))
            Session.Service().Navigate(Session.Surface(), kSteps[i].Direction);
    }
    ImGui::EndDisabled();
}

void PreviewPanel::DrawImage()
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 8.0f || avail.y < 8.0f || !Session.IsOpen())
    {
        Session.SetPlacement(std::nullopt);
        if (!Session.IsOpen())
            ImGui::TextDisabled("No document open. Pick one from Documents.");
        return;
    }

    const RenderExtent surface = Session.Resolution();
    const ImVec2 size = ImageSize(View.ZoomLevel, surface, avail);

    // Larger than the region scrolls; smaller is centred on the ground.
    if (!ImGui::BeginChild("##canvas", avail, ImGuiChildFlags_None,
                           ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove))
    {
        ImGui::EndChild();
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 offset{ std::max(0.0f, (avail.x - size.x) * 0.5f), std::max(0.0f, (avail.y - size.y) * 0.5f) };
    const ImVec2 imagePos{ origin.x + offset.x, origin.y + offset.y };
    ImGui::SetCursorScreenPos(imagePos);

    const ImTextureID texture = Target.Display(Binding);
    if (texture != 0)
        ImGui::Image(texture, size);
    else
        ImGui::Dummy(size);
    ImGui::SetCursorScreenPos(imagePos);
    // Where the image landed, in window points: the surface's placement. The
    // layer maps Interact-mode events through this; Inspect maps its hover
    // through the same function, so the two never disagree by a pixel.
    const Rect2d placement(imagePos.x, imagePos.y, size.x, size.y);
    Session.SetPlacement(placement);

    // The surface's own bounds. Without them a fitted document sits on a
    // ground its own background matches and there is no telling where the
    // screen ends and the letterbox begins.
    ImGui::GetWindowDrawList()->AddRect(imagePos, { imagePos.x + size.x, imagePos.y + size.y },
                                        ImGui::GetColorU32(EditorUi::Border));

    // An Image is never an item; the invisible button is what hover and click
    // are read from in Inspect mode. In Interact mode the layer consumed the
    // events inside the image before ImGui saw them, so this stays idle.
    ImGui::InvisibleButton("##surface", size, ImGuiButtonFlags_MouseButtonLeft);

    if (Session.Mode() == UiPreviewSession::PointerMode::Inspect)
        DrawInspection(placement);
    if (View.SafeArea)
        DrawSafeArea(placement);

    ImGui::EndChild();
}

void PreviewPanel::DrawInspection(const Rect2d& placement)
{
    UiService& ui = Session.Service();
    if (ImGui::IsItemHovered())
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        if (const std::optional<Vec2d> point =
                MapWindowPointToSurface(Vec2d(mouse.x, mouse.y), placement, Session.Resolution()))
        {
            View.Hovered = ui.ElementAt(Session.Surface(), *point);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            View.Selected = View.Hovered;
    }
    else
    {
        View.Hovered = {};
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ToImVec(placement.Min());
    const ImVec2 mx = ToImVec(placement.Max());
    dl->PushClipRect(mn, mx, true);
    if (View.Hovered.IsValid() && View.Hovered != View.Selected)
        DrawOutline(placement, View.Hovered, ImGui::GetColorU32(EditorUi::Accent), 1.0f);
    if (View.Selected.IsValid())
        DrawOutline(placement, View.Selected, ImGui::GetColorU32(EditorUi::SelectedOutline), 2.0f);
    dl->PopClipRect();
}

void PreviewPanel::DrawOutline(const Rect2d& placement, UiElementRef ref, ImU32 color, float thickness)
{
    const std::optional<UiElementInfo> info = Session.Service().DescribeElement(ref);
    if (!info)
        return;
    const RenderExtent surface = Session.Resolution();
    const UiElementBox& box = info->Boxes.Border;
    const ImVec2 mn = ToImVec(MapSurfacePointToWindow(Vec2d(box.X, box.Y), placement, surface));
    const ImVec2 mx = ToImVec(
        MapSurfacePointToWindow(Vec2d(box.X + box.Width, box.Y + box.Height), placement, surface));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(mn, mx, color, 0.0f, 0, thickness);
    // The padding box, dashed by tone rather than pattern: a second, dimmer rect.
    const UiElementBox& content = info->Boxes.Content;
    const ImVec2 cmn = ToImVec(MapSurfacePointToWindow(Vec2d(content.X, content.Y), placement, surface));
    const ImVec2 cmx = ToImVec(MapSurfacePointToWindow(
        Vec2d(content.X + content.Width, content.Y + content.Height), placement, surface));
    ImVec4 dim = ImGui::ColorConvertU32ToFloat4(color);
    dim.w *= 0.45f;
    dl->AddRect(cmn, cmx, ImGui::GetColorU32(dim), 0.0f, 0, 1.0f);
}

void PreviewPanel::DrawSafeArea(const Rect2d& placement)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ToImVec(placement.Min());
    const ImVec2 mx = ToImVec(placement.Max());
    const auto inset = [&](float fraction, ImU32 color) {
        const float dx = placement.Size.X * fraction;
        const float dy = placement.Size.Y * fraction;
        dl->AddRect({ mn.x + dx, mn.y + dy }, { mx.x - dx, mx.y - dy }, color, 0.0f, 0, 1.0f);
    };
    ImVec4 action = EditorUi::Warning;
    action.w = 0.6f;
    ImVec4 title = EditorUi::Warning;
    title.w = 0.9f;
    inset(0.05f, ImGui::GetColorU32(action));
    inset(0.10f, ImGui::GetColorU32(title));
}
