#include "ToolRegistry.h"

#include "ToolContext.h"
#include "../interaction/InteractionHost.h"

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

    if (ActiveIndex >= 0 && ActiveIndex < static_cast<int>(Tools.size())
        && Tools[ActiveIndex] != nullptr)
    {
        Tools[ActiveIndex]->OnDeactivate(Context);
    }

    Context.Interactions.Cancel(Context);

    ActiveIndex = static_cast<int>(index);
    Tools[ActiveIndex]->OnActivate(Context);
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

InputConsumed ToolRegistry::HandlePointerDown(EditorViewport& viewport, ImVec2 point)
{
    ITool* active = GetActiveTool();
    if (active == nullptr)
        return InputConsumed::No;

    return active->OnPointerDown(Context, viewport, point);
}

InputConsumed ToolRegistry::HandlePointerMove(EditorViewport& viewport, ImVec2 point, ImVec2 delta)
{
    ITool* active = GetActiveTool();
    if (active == nullptr)
        return InputConsumed::No;

    return active->OnPointerMove(Context, viewport, point, delta);
}

InputConsumed ToolRegistry::HandlePointerUp(EditorViewport& viewport, ImVec2 point)
{
    ITool* active = GetActiveTool();
    if (active == nullptr)
        return InputConsumed::No;

    return active->OnPointerUp(Context, viewport, point);
}

InputConsumed ToolRegistry::OnInput(const InputEvent& event)
{
    ITool* active = GetActiveTool();
    if (active == nullptr)
        return InputConsumed::No;

    if (const auto* e = std::get_if<KeyDownEvent>(&event))
        return active->OnKeyDown(Context, *e);

    return InputConsumed::No;
}
