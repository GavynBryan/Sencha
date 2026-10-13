#pragma once

#include "workspaces/WorkspaceFacets.h"

#include <functional>

class ActionCatalog;
class ActionContext;
class IPropertySource;
class ToolRegistry;

// The registry in effect when an action fires: a workspace may rebuild it.
using ToolRegistryResolver = std::function<ToolRegistry*()>;

// A tool registry as a workspace offers it: each tool an action (tool.<id>,
// under the key the tool declares, so a new tool needs no edit elsewhere), the
// held tool wheel named for the keymap, and a drafted operation's Apply and
// Cancel.
void OfferToolActions(ActionCatalog& catalog, ActionContext& context, const ToolRegistryResolver& tools);

// The tool in hand as the operation facet; `parameters` presents its settings.
[[nodiscard]] WorkspaceOperationFacet ToolOperation(ToolRegistry* tools, IPropertySource* parameters);
