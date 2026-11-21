#pragma once

#include <equipment/EquipmentComponents.h>
#include <world/ComponentSet.h>

class ComponentRegistrar;
class World;

using EquipmentComponents = ComponentSet<EquipmentState>;

void RegisterEquipmentComponents(ComponentRegistrar& registrar);

void InstallEquipmentVocabulary(World& world);
