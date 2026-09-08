#pragma once

#include <controller/LookOrientation.h>
#include <ecs/Query.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <movement/MovementIntent.h>
#include <world/transform/TransformComponents.h>

#include <optional>

struct FixedLogicContext;

// Turns this tick's resolved actions into movement intent, for every entity the
// movement vocabulary marks as controlled -- the player sitting at this
// machine and every peer whose commands arrived, each steering from its own
// input source and along its own aim.
struct FpsSteeringSystem
{
    void FixedLogic(FixedLogicContext& ctx);

private:
    std::optional<Query< Write<MovementIntent>, Read<GameplayTagContainer>, Read<LookOrientation>>> Steer;
};
