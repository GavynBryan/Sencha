#include "ViewportPanel.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <array>

ViewportPanel::ViewportPanel(FourWayViewportLayout& layout, ToolRegistry& tools)
    : Layout(layout)
    , Tools(tools)
{
}

std::string_view ViewportPanel::GetTitle() const
{
    return "Viewport";
}

bool ViewportPanel::IsVisible() const
{
    return Visible;
}

void ViewportPanel::OnDraw()
{
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const float menuHeight = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(
        ImVec2(mainViewport->Pos.x, mainViewport->Pos.y + menuHeight),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(mainViewport->Size.x, mainViewport->Size.y - menuHeight),
        ImGuiCond_Always);

    const ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin(GetTitle().data(), &Visible, windowFlags))
    {
        ImGui::End();
        return;
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 cellSize(
        (available.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f,
        (available.y - ImGui::GetStyle().ItemSpacing.y) * 0.5f);

    const std::array labels = { "Perspective", "Top", "Front", "Side" };

    for (int index = 0; index < 4; ++index)
    {
        if ((index % 2) != 0)
            ImGui::SameLine();

        ImGui::PushID(index);
        DrawViewport(Layout.Viewports[index], "ViewportRegion", labels[index], index, cellSize);
        ImGui::PopID();
    }

    ImGui::End();
}

bool ViewportPanel::ProcessSdlEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    {
        const ImVec2 point(event.button.x, event.button.y);
        const bool captureFlyCamera = event.button.button == SDL_BUTTON_RIGHT;
        const bool captureOrthoPan = event.button.button == SDL_BUTTON_MIDDLE;
        const bool handledViewport = UpdateActiveViewport(point, captureFlyCamera, captureOrthoPan);
        if (!handledViewport)
            return false;

        if (event.button.button == SDL_BUTTON_LEFT)
        {
            EditorViewport* activeViewport = Layout.GetActiveViewport();
            return activeViewport != nullptr
                ? (Tools.HandleViewportClick(*activeViewport, point) || handledViewport)
                : handledViewport;
        }

        return handledViewport;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        if (event.button.button != SDL_BUTTON_RIGHT
            && event.button.button != SDL_BUTTON_MIDDLE)
            return false;

        ClearViewportCapture();
        return true;
    }

    case SDL_EVENT_MOUSE_MOTION:
    {
        EditorViewport* activeViewport = Layout.GetActiveViewport();
        if (activeViewport != nullptr
            && activeViewport->WantsFlyCameraInput
            && activeViewport->Camera.ActiveMode == EditorCamera::Mode::Perspective)
        {
            activeViewport->Camera.ApplyPerspectiveLook(event.motion.xrel, event.motion.yrel);
            return true;
        }

        if (activeViewport != nullptr
            && activeViewport->WantsOrthoPanInput
            && activeViewport->Camera.ActiveMode == EditorCamera::Mode::Orthographic)
        {
            const float viewportHeight = activeViewport->RegionMax.y - activeViewport->RegionMin.y;
            activeViewport->Camera.ApplyOrthoPan(event.motion.xrel, event.motion.yrel, viewportHeight);
            return true;
        }

        return UpdateActiveViewport(ImVec2(event.motion.x, event.motion.y), false, false);
    }

    case SDL_EVENT_MOUSE_WHEEL:
    {
        EditorViewport* activeViewport = Layout.GetActiveViewport();
        if (activeViewport == nullptr)
            return false;

        if (activeViewport->Camera.ActiveMode == EditorCamera::Mode::Orthographic)
            activeViewport->Camera.ApplyOrthoZoom(event.wheel.y);
        else
            activeViewport->Camera.ApplyPerspectiveDolly(event.wheel.y);
        return true;
    }

    default:
        return false;
    }
}

void ViewportPanel::DrawViewport(EditorViewport& viewport,
                                 const char* id,
                                 const char* label,
                                 int index,
                                 ImVec2 size)
{
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
    viewport.RegionMin = ImGui::GetWindowPos();
    viewport.RegionMax = ImVec2(viewport.RegionMin.x + ImGui::GetWindowSize().x,
                                viewport.RegionMin.y + ImGui::GetWindowSize().y);
    viewport.IsActive = Layout.ActiveIndex == index;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRect(viewport.RegionMin, viewport.RegionMax, IM_COL32(70, 70, 70, 255));

    ImGui::SetCursorScreenPos(ImVec2(viewport.RegionMin.x + 8.0f, viewport.RegionMin.y + 6.0f));
    ImGui::TextUnformatted(label);
    ImGui::EndChild();
}

void ViewportPanel::ClearViewportCapture()
{
    for (EditorViewport& viewport : Layout.Viewports)
    {
        viewport.WantsFlyCameraInput = false;
        viewport.WantsOrthoPanInput = false;
    }
}

bool ViewportPanel::UpdateActiveViewport(ImVec2 point, bool captureFlyCamera, bool captureOrthoPan)
{
    for (int index = 0; index < 4; ++index)
    {
        EditorViewport& viewport = Layout.Viewports[index];
        if (!viewport.Contains(point))
            continue;

        Layout.ActiveIndex = index;
        for (int viewportIndex = 0; viewportIndex < 4; ++viewportIndex)
            Layout.Viewports[viewportIndex].IsActive = viewportIndex == index;
        if (captureFlyCamera || captureOrthoPan)
        {
            ClearViewportCapture();
            if (captureFlyCamera && viewport.Camera.ActiveMode == EditorCamera::Mode::Perspective)
                viewport.WantsFlyCameraInput = true;
            if (captureOrthoPan && viewport.Camera.ActiveMode == EditorCamera::Mode::Orthographic)
                viewport.WantsOrthoPanInput = true;
        }
        return true;
    }

    return false;
}
