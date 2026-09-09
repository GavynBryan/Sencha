#include "EditorToolSidebar.h"

#include "ui/EditorUiStyle.h"
#include "ui/chrome/ChromeBars.h"
#include "ui/chrome/ChromeControls.h"

#include "tools/ITool.h"
#include "tools/ToolRegistry.h"

#include <imgui.h>
#include <imgui_internal.h> // BeginViewportSideBar (reserves work-area space)

#include <string>

EditorToolSidebar::EditorToolSidebar(std::function<ToolRegistry*()> tools)
    : ToolsResolver(std::move(tools))
{
}

ToolRegistry& EditorToolSidebar::Tools() const
{
    return *ToolsResolver();
}

void EditorToolSidebar::Draw()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttonSize = ImGui::GetFrameHeight() * 1.15f;
    const float barWidth = buttonSize + style.WindowPadding.x * 2.0f;

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::BeginViewportSideBar("##EditorToolSidebar", viewport, ImGuiDir_Left, barWidth, flags))
    {
        EditorChrome::BarBackdrop(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(),
                                  ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                         ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                                  EditorChrome::BarEdge::Right);

        const auto& tools = Tools().GetTools();
        for (std::size_t i = 0; i < tools.size(); ++i)
        {
            const ITool* tool = tools[i].get();
            if (tool == nullptr)
                continue;

            const IconId icon = tool->GetIcon();
            const std::string name(tool->GetDisplayName());
            const bool active = Tools().GetActiveIndex() == static_cast<int>(i);
            const bool clicked = icon != IconId::None
                ? EditorChrome::ToolButton(tool->GetId().data(), icon, name.c_str(), active, buttonSize)
                : EditorChrome::ToolButton(tool->GetId().data(), name.c_str(), name.c_str(), active, buttonSize);
            if (clicked)
                Tools().Activate(i);
        }
    }
    ImGui::End();
}
