#include "actions/ActionFamily.h"

#include "actions/ActionOffer.h"

#include <algorithm>

void ActionFamily::Offer(ActionCatalog& catalog, ActionContext& context, std::vector<ActionDefinition> members,
                         const std::function<ActionBinding(std::size_t)>& bind)
{
    std::vector<std::string> ids;
    std::vector<std::string> labels;
    for (const ActionDefinition& member : members)
    {
        ids.push_back(member.Id);
        labels.push_back(member.Label);
    }
    if (ids == Ids && labels == Labels)
        return;

    for (const std::string& id : Ids)
        if (std::ranges::find(ids, id) == ids.end())
            context.Release(id);
    for (std::size_t i = 0; i < members.size(); ++i)
    {
        if (const ActionDefinition* existing = catalog.Find(ids[i]); existing != nullptr && existing->Label != labels[i])
            (void)catalog.Relabel(ids[i], labels[i]);
        OfferAction(catalog, context, std::move(members[i]), bind(i));
    }
    Ids = std::move(ids);
    Labels = std::move(labels);
}
