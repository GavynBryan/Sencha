#pragma once

#include <equipment/EquipmentComponents.h>
#include <world/ComponentSet.h>

class ComponentRegistrar;
class EngineSchedule;
class World;

using EquipmentComponents = ComponentSet<EquipmentState>;

void RegisterEquipmentComponents(ComponentRegistrar& registrar);

// Installs inventory's vocabulary first: equipment is built on it.
void InstallEquipmentVocabulary(World& world);

// Registers inventory's systems too when the game has not.
void RegisterEquipmentSystems(EngineSchedule& schedule);
