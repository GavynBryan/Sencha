#include <inventory/InventoryKit.h>

#include <ecs/World.h>
#include <inventory/InventorySerializers.h>
#include <inventory/ItemCostProvider.h>
#include <world/ComponentRegistrar.h>

void RegisterInventoryComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<InventoryComponents>();
    registrar.AddSerializer(MakeItemEntrySerializer());
    registrar.AddSerializer(MakeItemContainerSerializer());
    registrar.AddSerializer(MakeItemPickupSerializer());
}

void InstallInventoryVocabulary(World& world)
{
    if (!world.HasResource<ItemRegistry>())
        world.AddResource<ItemRegistry>();
    DeclareItemCostProvider(world);
}
