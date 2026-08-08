#pragma once

#include <abilities/AbilityActivationSystem.h>
#include <app/EngineSchedule.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/MovementModeSystems.h>

class ComponentRegistrar;
class DataAssetCache;
class World;

//=============================================================================
// Movement registration
//
// Movement is built on the ability kit (tags/attributes/effects/abilities):
// the component setup pulls the kit in, jump is authored as an ability, and
// MoveSpeed is an attribute. The system pipeline consumes the kit's fixed-tick
// systems, so register those first (RegisterAbilityKitSystems) before
// RegisterMovementSystems.
//=============================================================================

// The movement component vocabulary, including the ability kit's. Movement is
// built on the kit, so a caller cannot get one without the other.
void RegisterMovementComponents(ComponentRegistrar& registrar);

// The same components plus the per-World pieces a sealed vocabulary cannot
// carry: the movement.* tags and the LocomotionModeRegistry holding the
// built-in Free mode. Calls RegisterAbilityKit. No default abilities.
void RegisterMovementComponents(World& world);

// The default MoveSpeed attribute and the Jump ability/effects, authored as
// data.
void RegisterDefaultMovementAbilities(World& world);

// Convenience: RegisterMovementComponents + RegisterDefaultMovementAbilities.
void RegisterMovement(World& world);

// The built-in movement fixed-tick systems with the full pipeline order,
// including the interleaving edges onto the ability-kit systems. `dataAssets`
// backs the profile binding cache the tuning system resolves through. Call
// RegisterAbilityKitSystems first.
void RegisterMovementSystems(EngineSchedule& schedule, DataAssetCache& dataAssets);

// Order the movement pipeline after a game's input system (which produces
// MovementIntent and queues ability activations). Register both first.
template <typename TInputSystem>
void OrderMovementAfterInput(EngineSchedule& schedule)
{
    schedule.After<AbilityActivationSystem, TInputSystem>();
    schedule.After<FreeLocomotionSystem, TInputSystem>();
    schedule.After<ModeRequestCollectionSystem, TInputSystem>();
}
