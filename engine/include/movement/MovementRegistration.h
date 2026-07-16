#pragma once

#include <abilities/AbilityActivationSystem.h>
#include <app/EngineSchedule.h>
#include <movement/AirLocomotionSystem.h>
#include <movement/GroundLocomotionSystem.h>

class EntityStore;
class ResourceStore;

//=============================================================================
// Movement registration
//
// Movement is built on the ability kit (tags/attributes/effects/abilities): the
// component setup pulls the kit's components in, jump is authored as an ability,
// and MoveSpeed is an attribute. The system pipeline consumes the kit's fixed-tick
// systems, so register those first (RegisterAbilityKitSystems) before
// RegisterMovementSystems.
//=============================================================================

// Movement components/markers plus the ability-kit components. Registered per
// registry that hosts movement-driven pawns.
void RegisterMovementComponents(EntityStore& world);

// Movement session definitions: the movement.* tags, the built-in locomotion mode
// entries (OnGround/InAir), the default MoveSpeed attribute, and the Jump
// ability/effects, plus the ability-kit definitions. Registered once in the
// global registry's resources and shared by every zone.
void RegisterMovementDefinitions(ResourceStore& sessionResources);

// The built-in movement fixed-tick systems (grounding, mode arbiter, jump,
// ground/air locomotion) with the full pipeline order, including the interleaving
// edges onto the ability-kit systems. Call RegisterAbilityKitSystems first.
void RegisterMovementSystems(EngineSchedule& schedule);

// Order the movement pipeline after a game's input system (which produces
// MovementIntent and queues ability activations). Register both first.
template <typename TInputSystem>
void OrderMovementAfterInput(EngineSchedule& schedule)
{
    schedule.After<AbilityActivationSystem, TInputSystem>();
    schedule.After<GroundLocomotionSystem, TInputSystem>();
    schedule.After<AirLocomotionSystem, TInputSystem>();
}
