#pragma once

#include <input/InputTypes.h>
#include <physics/RigidBody2D.h>
#include <transform/TransformStore.h>
#include <vector>
#include "Player.h"

class SdlInputSystem;

//=============================================================================
// PlayerSystem
//
// Reads input and expresses player intent as velocity on the player's
// RigidBody2D each frame. Actual movement and collision resolution are
// handled by RigidBodyResolutionSystem2D in the Fixed lane.
//
// Must run after SdlInputSystem in the Frame lane.
//=============================================================================
class PlayerSystem
{
public:
    struct Actions
    {
        InputActionId MoveUp;
        InputActionId MoveDown;
        InputActionId MoveLeft;
        InputActionId MoveRight;
        InputActionId ShiftEyeLeft;
        InputActionId ShiftEyeRight;
        InputActionId Quit;
    };

    PlayerSystem(SdlInputSystem&         input,
                 std::vector<Player>&    players,
                 RigidBodyStore&         bodies,
                 TransformStore<Transform2f>& transforms,
                 const Actions&          actions);

    void Update(float dt);

    bool WantsQuit() const { return QuitRequested; }

private:
    SdlInputSystem&         Input;
    std::vector<Player>&    Players;
    RigidBodyStore&         Bodies;
    TransformStore<Transform2f>& Transforms;
    Actions                 ActionIds;
    bool                    QuitRequested = false;
};
