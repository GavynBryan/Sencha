#pragma once

#include <input/InputTypes.h>
#include <world/entity/EntityBatch.h>
#include "Player.h"

class SdlInputSystem;
class PhysicsDomain2D;

//=============================================================================
// PlayerSystem
//
// Owns all player behaviour for one frame: reads input and moves the player
// through the physics domain. Visual representation is handled externally by
// SpriteRenderSystem.
//
// SystemHost lanes used:
//   Update(float dt) — reads input events and applies movement
//
// PlayerSystem must run after SdlInputSystem in the Frame lane so that
// input events are already populated when Update fires.
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
        InputActionId Quit;
    };

    PlayerSystem(SdlInputSystem& input,
                 EntityBatch<Player>& players,
                 PhysicsDomain2D& physics,
                 const Actions& actions);

    void Update(float dt);

    bool WantsQuit() const { return QuitRequested; }

private:
    SdlInputSystem&      Input;
    EntityBatch<Player>& Players;
    PhysicsDomain2D&     Physics;
    Actions              ActionIds;
    bool                 QuitRequested = false;
};
