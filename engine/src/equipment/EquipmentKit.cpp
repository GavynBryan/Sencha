#include <equipment/EquipmentKit.h>

#include <abilities/AbilitySystem.h>
#include <app/EngineSchedule.h>
#include <ecs/World.h>
#include <equipment/EquipmentSerializers.h>
#include <equipment/EquipmentSystem.h>
#include <equipment/EquipmentVocabulary.h>
#include <inventory/InventoryKit.h>
#include <inventory/InventorySystem.h>
#include <world/ComponentRegistrar.h>

void RegisterEquipmentComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<EquipmentComponents>();
    registrar.AddSerializer(MakeEquipmentStateSerializer());
}

void InstallEquipmentVocabulary(World& world)
{
    InstallInventoryVocabulary(world);
    if (!world.HasResource<EquipmentLayoutRegistry>())
        world.AddResource<EquipmentLayoutRegistry>();
    if (!world.HasResource<EquipmentRecordLists>())
        world.AddResource<EquipmentRecordLists>();
    (void)InstallEquipmentMoveAdmission(world);
    DeclareSlotCostProvider(world);
}

void RegisterEquipmentSystems(EngineSchedule& schedule)
{
    if (schedule.Has<EquipmentSystem>())
        return;
    RegisterInventorySystems(schedule);
    schedule.Register<EquipmentSystem>();
    schedule.After<EquipmentSystem, AbilitySystem>();
    schedule.After<InventorySystem, EquipmentSystem>();
}
