#include "EditorToolSidebar.h"

#include "ui/EditorUiSkin.h"
#include "ui/EditorUiStyle.h"

#include "tools/ITool.h"
#include "tools/ToolRegistry.h"

#include <imgui.h>
#include <imgui_internal.h> // BeginViewportSideBar (reserves work-area space)

#include <string>

EditorToolSidebar::EditorToolSidebar(ToolRegistry& tools)
    : Tools(tools)
{
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
        EditorUiSkin::Band(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(),
                           ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                  ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                           EditorUi::HeaderBg);

        const auto& tools = Tools.GetTools();
        for (std::size_t i = 0; i < tools.size(); ++i)
        {
            const ITool* tool = tools[i].get();
            if (tool == nullptr)
                continue;

            const std::string_view iconView = tool->GetIcon();
            const std::string icon(iconView.empty() ? tool->GetDisplayName() : iconView);
            const bool active = Tools.GetActiveIndex() == static_cast<int>(i);
            if (EditorUiSkin::Button(tool->GetId().data(), icon.c_str(),
                                     ImVec2(buttonSize, buttonSize), active))
                Tools.Activate(i);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tool->GetDisplayName().data());
        }
    }
    ImGui::End();
}
