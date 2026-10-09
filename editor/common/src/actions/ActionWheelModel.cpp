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
    if (const ActionHandler* handler = Context.Find(Shown[static_cast<std::size_t>(index)]);
        handler != nullptr && handler->Invoke && (!handler->Enabled || handler->Enabled()))
        handler->Invoke();
}

void ActionWheelModel::Opening(Vec2d)
{
    Shown.clear();
    for (const std::string& id : Ids)
        if (const ActionHandler* handler = Context.Find(id);
            handler != nullptr && Catalog.Find(id) != nullptr && (!handler->Enabled || handler->Enabled()))
            Shown.push_back(id);
}
