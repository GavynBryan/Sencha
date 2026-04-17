#pragma once

#include <core/batch/DataBatch.h>
#include <input/InputTypes.h>
#include <physics/2d/RigidBody2D.h>
#include <transform/TransformStore.h>
#include <world/entity/EntityBatch.h>
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
                 EntityBatch<Player>&    players,
                 DataBatch<RigidBody2D>& bodies,
                 TransformStore<Transform2f>& transforms,
                 const Actions&          actions);

    void Update(float dt);

    bool WantsQuit() const { return QuitRequested; }

private:
    SdlInputSystem&         Input;
    EntityBatch<Player>&    Players;
    DataBatch<RigidBody2D>& Bodies;
    TransformStore<Transform2f>& Transforms;
    Actions                 ActionIds;
    bool                    QuitRequested = false;
};
