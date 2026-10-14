#pragma once

#include <app/GameContexts.h>
#include <ecs/Query.h>
#include <movement/MovementIntent.h>
#include <movement/components/CharacterMovement.h>
#include <time/ActorTime.h>

#include <optional>
#include <utility>
#include <vector>

class FreeLocomotionSystem;
class PhysicsStepSystem;

// Steps every character whose time is dilated, one at a time, through the same
// kernel a replay runs (StepScaledCharacterTick), after the bulk systems have
// stepped everyone else. The bulk systems skip a character carrying
// ActorTimeDilated, so each character is stepped exactly once a tick.
class DilatedCharacterStepSystem
{
public:
    DilatedCharacterStepSystem(const FreeLocomotionSystem& locomotion, PhysicsStepSystem* physics);

    void FixedLogic(FixedLogicContext& ctx);

private:
    const FreeLocomotionSystem* Locomotion;
    PhysicsStepSystem* Physics;
    std::optional<Query<Read<ActorTime>, Read<MovementIntent>, Read<CharacterMovement>, With<ActorTimeDilated>>> Dilated;
    std::vector<std::pair<EntityId, MovementIntent>> Pending;
};
