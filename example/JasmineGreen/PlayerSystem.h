#pragma once

#include <input/InputTypes.h>
#include <render/backend/vulkan/VulkanDescriptorCache.h>
#include <render/features/SpriteFeature.h>
#include <world/entity/EntityBatch.h>
#include "Player.h"

class SdlInputSystem;
class PhysicsDomain2D;

//=============================================================================
// PlayerSystem
//
// Owns all player behaviour for one frame: reads input, moves the player
// through the physics domain, and submits the player's sprites for rendering.
//
// SystemHost lanes used:
//   Update(float dt)    — reads input events and applies movement
//   Render(float alpha) — submits sprite draw calls to SpriteFeature
//
// PlayerSystem must run after SdlInputSystem in the Frame lane so that
// input events are already populated when Update fires. Declare this with
// systems.After<PlayerSystem, SdlInputSystem>() before systems.Init().
//
// Physics note: movement goes through PhysicsDomain2D::MoveBox even though
// there are no static colliders yet. This keeps the path future-proof —
// walls, platforms, and trigger zones can be added later without touching
// the player movement code.
//=============================================================================
class PlayerSystem
{
public:
    // Resolved action IDs for this game's input bindings. Built once at
    // startup from InputActionRegistry::ResolveAction and passed in here.
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
                 SpriteFeature& sprites,
                 BindlessImageIndex whitePixel,
                 const Actions& actions);

    // Frame lane — reads held-direction events and resolves movement.
    void Update(float dt);

    // Render lane — submits the body and eye sprites for this frame.
    void Render(float alpha);

    bool WantsQuit() const { return QuitRequested; }

private:
    SdlInputSystem&      Input;
    EntityBatch<Player>& Players;
    PhysicsDomain2D&     Physics;
    SpriteFeature&       Sprites;
    BindlessImageIndex   WhitePixel;
    Actions              ActionIds;
    bool                 QuitRequested = false;
};
