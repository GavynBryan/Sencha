#pragma once

class EngineSchedule;
class World;

// Registers the controller components: a locally controlled entity carries a
// LookOrientation and a LocalLookControl tag, and one whose body turns with its
// aim carries an AimFacing tag.
void RegisterControllerComponents(World& world);

// Registers the look integration and aim facing systems, ordered against each
// other. Order the pair after the input resolve system; anything else that
// reads the resulting orientation runs after this.
//
// Call before RegisterNetSystems: the net side declares edges naming
// AimFacingSystem, and an edge naming a system the schedule does not have yet
// cannot be declared.
void RegisterControllerSystems(EngineSchedule& schedule);
