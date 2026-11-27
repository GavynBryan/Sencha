#include <equipment/EquipmentKit.h>

#include <ecs/World.h>
#include <equipment/EquipmentSerializers.h>
#include <equipment/EquipmentSystem.h>
#include <equipment/EquipmentVocabulary.h>
#include <world/ComponentRegistrar.h>

void RegisterEquipmentComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<EquipmentComponents>();
    registrar.AddSerializer(MakeEquipmentStateSerializer());
}

void InstallEquipmentVocabulary(World& world)
{
    if (!world.HasResource<EquipmentLayoutRegistry>())
        world.AddResource<EquipmentLayoutRegistry>();
    InstallEquipmentMoveAdmission(world);
    DeclareSlotCostProvider(world);
}
