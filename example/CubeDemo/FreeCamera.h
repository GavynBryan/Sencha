#pragma once

#include <ecs/EntityId.h>
#include <ecs/EntityStore.h>
#include <input/InputFrame.h>
#include <math/geometry/3d/Transform3d.h>

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
    void TickFixed(const InputFrame& input, EntityStore& world, float fixedDt);
    void ApplyRotation(EntityStore& world) const;
};
