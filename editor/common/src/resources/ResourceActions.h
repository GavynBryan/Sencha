#pragma once

#include <core/assets/AssetRef.h>

#include <cstddef>
#include <functional>
#include <string>

class ActionCatalog;
class ActionContext;
class ActiveResources;

// Offers the stash of whichever kind `primary` names at the time, as actions in
// the Resource category: use, store into and clear each slot.
void OfferResourceActions(ActionCatalog& catalog,
                          ActionContext& context,
                          ActiveResources& resources,
                          std::function<AssetType()> primary);
// The action that puts stash slot `slot` in hand.
[[nodiscard]] std::string UseSlotAction(std::size_t slot);
