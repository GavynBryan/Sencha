#include "actions/ActionWheelModel.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"

#include <utility>

ActionWheelModel::ActionWheelModel(const ActionCatalog& catalog, const ActionContext& context,
                                   std::vector<std::string> ids)
    : Catalog(catalog)
    , Context(context)
    , Ids(std::move(ids))
{
}

IRadialMenuModel::MenuItem ActionWheelModel::Item(int index) const
{
    const ActionDefinition* definition =
        index >= 0 && index < Count() ? Catalog.Find(Shown[static_cast<std::size_t>(index)]) : nullptr;
    return definition != nullptr ? MenuItem{ definition->Label, definition->Icon } : MenuItem{};
}

void ActionWheelModel::Select(int index, int)
{
    if (index < 0 || index >= Count())
        return;
    if (const ActionBinding* binding = Context.Find(Shown[static_cast<std::size_t>(index)]))
        (void)binding->TryInvoke();
}

void ActionWheelModel::Opening(Vec2d)
{
    Shown.clear();
    for (const std::string& id : Ids)
        if (const ActionBinding* binding = Context.Find(id);
            binding != nullptr && Catalog.Find(id) != nullptr && binding->IsEnabled())
            Shown.push_back(id);
}
