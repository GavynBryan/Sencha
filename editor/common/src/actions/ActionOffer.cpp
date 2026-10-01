#include "actions/ActionOffer.h"

#include <SDL3/SDL_keycode.h>

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

ActionDefinition StepBackAction()
{
    return { .Id = "edit.escape", .Label = "Step Back", .Category = "Edit", .DefaultChords = { { SDLK_ESCAPE, {} } } };
}

ActionDefinition FrameAction()
{
    return { .Id = "view.frame_selection", .Label = "Frame Selection", .Category = "View",
             .DefaultChords = { { SDLK_F, {} } } };
}
