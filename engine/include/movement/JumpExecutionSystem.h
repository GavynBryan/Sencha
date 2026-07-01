#pragma once

#include <ecs/Query.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <movement/MovementProfile.h>
#include <physics/components/CharacterController.h>

#include <optional>

struct FixedLogicContext;
class World;

//=============================================================================
// Jump execution: consumes the movement.jump.requested tag into
// CharacterController.PendingJumpSpeed (single fire). The physics mover applies
// the impulse. Jump is authored data (an ability granting the request tag), not a
// code path; this only translates the granted tag into a pending impulse.
//=============================================================================
class JumpExecutionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    void Step(World& world);

private:
    const World* LastWorld = nullptr;
    std::optional<Query<Write<GameplayTagContainer>, Write<CharacterController>,
                        Read<MovementProfile>>> CachedQuery;
};
