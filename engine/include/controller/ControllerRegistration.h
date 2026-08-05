#pragma once

class EngineSchedule;
class World;

// Registers the controller components: a locally controlled entity carries a
// LookOrientation and a LocalLookControl tag.
void RegisterControllerComponents(World& world);

// Registers the look integration system. Order it after the input resolve
// system; anything that reads the resulting orientation runs after this.
void RegisterControllerSystems(EngineSchedule& schedule);
