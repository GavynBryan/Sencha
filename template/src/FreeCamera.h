#pragma once

#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <input/InputFrame.h>
#include <math/geometry/3d/Transform3d.h>

// Host fly-camera: mouse-look while the right button is held, WASD/QE move.
// Pure input-driven transform mutation, no gameplay state. The starter game
// binds one to the active camera so any cooked scene is viewable even when it
// carries no camera of its own. Delete this once the game has its own camera.
struct FreeCamera
{
    EntityId Entity;
    float Yaw = 0.0f;
    float Pitch = 0.0f;
    float MoveSpeed = 4.0f;
    float FastMultiplier = 4.0f;
    float MouseSensitivity = 0.0025f;
    bool LookHeld = false;

    void UpdateLook(const InputFrame& input);
    void TickFixed(const InputFrame& input, World& world, float fixedDt);
    void ApplyRotation(World& world) const;
};
