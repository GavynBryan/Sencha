#include "ToolRegistry.h"

#include "ToolContext.h"

ToolRegistry::ToolRegistry(ToolContext& context)
    : Context(context)
{
}

void ToolRegistry::Register(std::unique_ptr<ITool> tool)
{
    if (tool == nullptr)
        return;

    Tools.push_back(std::move(tool));
    if (ActiveIndex < 0)
        ActiveIndex = 0;
}

bool ToolRegistry::Activate(std::string_view id)
{
    for (std::size_t index = 0; index < Tools.size(); ++index)
    {
        if (Tools[index] != nullptr && Tools[index]->GetId() == id)
            return Activate(index);
    }

    return false;
}

bool ToolRegistry::Activate(std::size_t index)
{
    if (index >= Tools.size() || Tools[index] == nullptr)
        return false;

    ActiveIndex = static_cast<int>(index);
    return true;
}

ITool* ToolRegistry::GetActiveTool()
{
    return const_cast<ITool*>(static_cast<const ToolRegistry*>(this)->GetActiveTool());
}

const ITool* ToolRegistry::GetActiveTool() const
{
    if (ActiveIndex < 0 || ActiveIndex >= static_cast<int>(Tools.size()))
        return nullptr;

    return Tools[ActiveIndex].get();
}

int ToolRegistry::GetActiveIndex() const
{
    return ActiveIndex;
}

const std::vector<std::unique_ptr<ITool>>& ToolRegistry::GetTools() const
{
    return Tools;
}

bool ToolRegistry::HandleViewportClick(EditorViewport& viewport, ImVec2 point)
{
    ITool* activeTool = GetActiveTool();
    if (activeTool == nullptr)
        return false;

    return activeTool->OnViewportClick(Context, viewport, point);
}
