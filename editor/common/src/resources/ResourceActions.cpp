#include "resources/ResourceActions.h"

#include "actions/ActionOffer.h"
#include "resources/ActiveResources.h"

#include <format>

void OfferResourceActions(ActionCatalog& catalog,
                          ActionContext& context,
                          ActiveResources& resources,
                          std::function<AssetType()> primary)
{
    for (std::size_t slot = 0; slot < ActiveResources::kStashSlots; ++slot)
    {
        const std::size_t number = slot + 1;
        OfferAction(catalog, context,
                    { .Id = UseSlotAction(slot),
                      .Label = std::format("Use Slot {}", number),
                      .Category = "Resource" },
                    { .Invoke = [&resources, primary, slot] { resources.UseStashSlot(primary(), slot); },
                      .Enabled = [&resources, primary, slot] {
                          const AssetType type = primary();
                          return resources.Stash(type)[slot].IsValid() || resources.Active(type).IsValid();
                      } });
        OfferAction(catalog, context,
                    { .Id = std::format("resource.store.{}", number),
                      .Label = std::format("Store in Slot {}", number),
                      .Category = "Resource" },
                    { .Invoke = [&resources, primary, slot] { resources.StoreInStash(primary(), slot); },
                      .Enabled = [&resources, primary] { return resources.Active(primary()).IsValid(); } });
        OfferAction(catalog, context,
                    { .Id = std::format("resource.clear.{}", number),
                      .Label = std::format("Clear Slot {}", number),
                      .Category = "Resource" },
                    { .Invoke = [&resources, primary, slot] { resources.ClearStashSlot(primary(), slot); },
                      .Enabled = [&resources, primary, slot] { return resources.Stash(primary())[slot].IsValid(); } });
    }
}

std::string UseSlotAction(std::size_t slot)
{
    return std::format("resource.slot.{}", slot + 1);
}
