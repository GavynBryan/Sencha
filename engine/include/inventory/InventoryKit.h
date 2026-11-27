#pragma once

#include <inventory/InventoryComponents.h>
#include <world/ComponentSet.h>

class ComponentRegistrar;
class World;

using InventoryComponents = ComponentSet<ItemEntry, ItemContainer, ItemPickup>;

void RegisterInventoryComponents(ComponentRegistrar& registrar);

// After the ability kit's vocabulary, whose cost provider catalog it declares into. Idempotent.
void InstallInventoryVocabulary(World& world);
