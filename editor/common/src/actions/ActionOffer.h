#pragma once

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"

// Defines an action in the catalog and claims it in `context`: how a workspace
// offers an operation it performs. A definition that clashes with an existing
// one is reported and left as it was; the handler is claimed either way, so
// the operation still works under the existing definition. A handler with no
// Invoke claims nothing -- the action is only named, as a held key is.
void OfferAction(ActionCatalog& catalog, ActionContext& context, ActionDefinition definition, ActionHandler handler);

// Escape: whatever is open or in hand steps back one level. One action in every
// workspace, so every one of them answers the same key.
[[nodiscard]] ActionDefinition StepBackAction();

// F: the view turns to what is selected, or to the subject when nothing is.
[[nodiscard]] ActionDefinition FrameAction();

// Delete: whatever is picked goes, as one step.
// Ctrl+S: the document in hand is written, as the workspace showing it saves.
[[nodiscard]] ActionDefinition SaveAction();

[[nodiscard]] ActionDefinition DeleteAction();
