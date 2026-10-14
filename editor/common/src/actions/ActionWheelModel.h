#pragma once

#include "tools/RadialMenuModel.h"

#include <string>
#include <vector>

class ActionCatalog;
class ActionContext;

// A wheel of actions: those of `ids` that `context` can perform as it opens,
// named and pictured as the catalog defines them, performed when chosen.
class ActionWheelModel final : public IRadialMenuModel
{
public:
    ActionWheelModel(const ActionCatalog& catalog, const ActionContext& context, std::vector<std::string> ids);

    [[nodiscard]] int Count() const override { return static_cast<int>(Shown.size()); }
    [[nodiscard]] MenuItem Item(int index) const override;
    [[nodiscard]] int ActiveIndex() const override { return -1; }
    void Select(int index, int variant) override;
    void Opening(Vec2d pointer) override;

private:
    const ActionCatalog& Catalog;
    const ActionContext& Context;
    std::vector<std::string> Ids;
    std::vector<std::string> Shown;
};
