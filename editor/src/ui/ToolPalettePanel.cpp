#include "ToolPalettePanel.h"

#include "ScopedPanel.h"

#include "../tools/ITool.h"
#include "../tools/ToolRegistry.h"

#include <imgui.h>

ToolPalettePanel::ToolPalettePanel(ToolRegistry& tools)
    : Tools(tools)
{
}

std::string_view ToolPalettePanel::GetTitle() const
{
    return "Tools";
}

void ToolPalettePanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    const auto& tools = Tools.GetTools();
    for (std::size_t index = 0; index < tools.size(); ++index)
    {
        if (tools[index] == nullptr)
            continue;

        const bool isActive = Tools.GetActiveIndex() == static_cast<int>(index);
        if (ImGui::Selectable(tools[index]->GetDisplayName().data(), isActive))
            Tools.Activate(index);
    }
}
