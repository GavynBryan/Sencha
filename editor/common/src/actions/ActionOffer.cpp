#include "actions/ActionOffer.h"

#include <cstdio>

void OfferAction(ActionCatalog& catalog, ActionContext& context, ActionDefinition definition, ActionHandler handler)
{
    const std::string id = definition.Id;
    std::string error;
    if (!catalog.Define(std::move(definition), &error))
        std::fprintf(stderr, "[editor] %s\n", error.c_str());
    if (handler.Invoke)
        context.Claim(id, std::move(handler));
}
