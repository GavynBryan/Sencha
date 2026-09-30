#include "tools/ToolParameterSource.h"

#include "tools/ITool.h"
#include "tools/ToolRegistry.h"

std::vector<PropertyNode> ToolParameterSource::Nodes() const
{
    const ToolRegistry* tools = Tools();
    const ITool* tool = tools != nullptr ? tools->GetActiveTool() : nullptr;
    return tool != nullptr ? tool->Parameters(tools->GetContext()) : std::vector<PropertyNode>{};
}

bool ToolParameterSource::BeginEdit(std::string_view key)
{
    for (const PropertyNode& node : Nodes())
    {
        if (node.Key != key)
            continue;
        EditKey = node.Key;
        EditBegan = node.Value;
        return true;
    }
    return false;
}

PropertyEditResult ToolParameterSource::PreviewEdit(std::string_view key, std::string_view value)
{
    return Set(key, value);
}

PropertyEditResult ToolParameterSource::CommitEdit(std::string_view key, std::string_view value)
{
    const PropertyEditResult result = Set(key, value);
    if (key == EditKey)
        EditKey.clear();
    return result;
}

void ToolParameterSource::CancelEdit(std::string_view key)
{
    if (key != EditKey)
        return;
    (void)Set(key, EditBegan);
    EditKey.clear();
}

PropertyEditResult ToolParameterSource::Set(std::string_view key, std::string_view value)
{
    ToolRegistry* tools = Tools();
    ITool* tool = tools != nullptr ? tools->GetActiveTool() : nullptr;
    return tool != nullptr ? tool->SetParameter(tools->GetContext(), key, value) : PropertyEditResult::Gone;
}
