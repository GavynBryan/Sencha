#pragma once

class EngineSchedule;

//=============================================================================
// RegisterPhysics
//
// Opt-in physics for a game: registers PhysicsStepSystem and
// CharacterControllerSystem in the Physics step, ordered so characters move
// against the stepped world. A game calls this once from OnRegisterSystems.
// Physics still only runs on zones that opt into Physics participation.
//=============================================================================
void RegisterPhysics(EngineSchedule& schedule);
