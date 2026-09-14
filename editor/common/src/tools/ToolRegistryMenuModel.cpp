#include "ToolRegistryMenuModel.h"

#include "ITool.h"
#include "ToolRegistry.h"

namespace
{
const ITool* ToolAt(const ToolRegistry& tools, int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= tools.GetTools().size())
        return nullptr;
    return tools.GetTools()[static_cast<std::size_t>(index)].get();
}
}

int ToolRegistryMenuModel::Count() const
{
    return static_cast<int>(Tools.GetTools().size());
}

IRadialMenuModel::MenuItem ToolRegistryMenuModel::Item(int index) const
{
    const ITool* tool = ToolAt(Tools, index);
    return tool != nullptr ? MenuItem{ .Label = tool->GetDisplayName(), .Icon = tool->GetIcon() } : MenuItem{};
}

int ToolRegistryMenuModel::ActiveIndex() const
{
    return Tools.GetActiveIndex();
}

std::span<const IRadialMenuModel::MenuItem> ToolRegistryMenuModel::Variants(int index) const
{
    const ITool* tool = ToolAt(Tools, index);
    return tool != nullptr ? tool->GetVariants() : std::span<const MenuItem>{};
}

int ToolRegistryMenuModel::ActiveVariant(int index) const
{
    const ITool* tool = ToolAt(Tools, index);
    return tool != nullptr ? tool->GetActiveVariant(Tools.GetContext()) : -1;
}

void ToolRegistryMenuModel::Select(int index, int variant)
{
    if (ToolAt(Tools, index) == nullptr)
        return;
    // A variant is chosen for the work that follows: the registry enters the
    // tool or places what it has staged, then selects the variant. The tool
    // alone is entered only if it is not already on, because re-entering a
    // tool reverts whatever it had pending.
    if (variant >= 0)
        (void)Tools.SelectVariant(static_cast<std::size_t>(index), static_cast<std::size_t>(variant));
    else if (index != Tools.GetActiveIndex())
        (void)Tools.Activate(static_cast<std::size_t>(index));
}
