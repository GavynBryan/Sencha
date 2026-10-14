#pragma once

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <vector>

// Actions whose members come and go with data: a cook profile each, a zone
// each. Offering the current members releases those offered before and gone
// now, and relabels one whose name changed; while they name the same actions
// under the same labels, nothing is bound again.
class ActionFamily
{
public:
    void Offer(ActionCatalog& catalog, ActionContext& context, std::vector<ActionDefinition> members,
               const std::function<ActionBinding(std::size_t)>& bind);
    [[nodiscard]] std::span<const std::string> Offered() const { return Ids; }

private:
    std::vector<std::string> Ids;
    std::vector<std::string> Labels;
};
