#include <inventory/InventoryKit.h>

#include <abilities/AbilitySystem.h>
#include <app/EngineSchedule.h>
#include <ecs/World.h>
#include <inventory/InventorySerializers.h>
#include <inventory/InventorySystem.h>
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

void RegisterInventorySystems(EngineSchedule& schedule)
{
    if (schedule.Has<InventorySystem>())
        return;
    schedule.Register<InventorySystem>();
    schedule.After<InventorySystem, AbilitySystem>();
}
