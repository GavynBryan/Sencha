#pragma once

#include <inventory/InventoryComponents.h>
#include <world/ComponentSet.h>

class ComponentRegistrar;
class World;

using InventoryComponents = ComponentSet<ItemEntry, ItemContainer, ItemPickup>;

void RegisterInventoryComponents(ComponentRegistrar& registrar);

void InstallInventoryVocabulary(World& world);
