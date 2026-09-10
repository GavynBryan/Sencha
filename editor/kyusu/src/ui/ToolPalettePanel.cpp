#include "ToolPalettePanel.h"

#include "ui/ScopedPanel.h"
#include "ui/ButtonFlow.h"
#include "ui/chrome/ChromeBars.h"
#include "tools/ITool.h"
#include "tools/ToolRegistry.h"

#include <utility>

ToolPalettePanel::ToolPalettePanel(std::function<ToolRegistry*()> tools)
    : ToolsResolver(std::move(tools))
{
}

void ToolPalettePanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), nullptr, PanelStyle::Compact,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!panel.IsOpen())
        return;
    EditorChrome::ModuleScope bay("tools");
    ToolRegistry* registry = ToolsResolver ? ToolsResolver() : nullptr;
    if (registry == nullptr || registry->GetTools().empty())
    {
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, EditorChrome::BarButtonSize()));
        return;
    }
    ButtonFlow flow;
    const auto& tools = registry->GetTools();
    for (std::size_t i = 0; i < tools.size(); ++i)
    {
        const ITool* tool = tools[i].get();
        if (tool == nullptr)
            continue;
        // Tool labels and IDs are registry-owned, null-terminated strings.
        const char* id = tool->GetId().data();
        const char* name = tool->GetDisplayName().data();
        const bool active = registry->GetActiveIndex() == static_cast<int>(i);
        const float size = EditorChrome::BarButtonSize() * 1.15f;
        const bool clicked = tool->GetIcon() != IconId::None
            ? flow.ToolButton(id, tool->GetIcon(), name, active, size)
            : flow.Button(name, active ? EditorChrome::ButtonTone::Active : EditorChrome::ButtonTone::Normal);
        if (clicked)
            registry->Activate(i);
    }
}
