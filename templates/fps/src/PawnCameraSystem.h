#pragma once

#include <ecs/EntityId.h>

class Engine;
class Logger;
struct FrameUpdateContext;

//=============================================================================
// PawnCameraSystem
//
// This game's camera policy: the player looks through the camera their body
// carries as a child, from inside it. The prefab places that child; the body's
// AimFacing turns it with the aim on the tick; this writes the one thing the
// hierarchy cannot -- the pitch -- and tells the renderer to leave the body
// itself out of the picture.
//
// Follows the local control subject, which the engine publishes. When it
// changes, the new body's camera child becomes the active camera and is given
// the exclusion; when there is none, nothing is active.
//=============================================================================
struct PawnCameraSystem
{
    Engine* Owner = nullptr;
    Logger* Log = nullptr;

    void FrameUpdate(FrameUpdateContext& ctx);

private:
    // The body last attached to and the camera child it was attached through,
    // so attaching is an edge and pitch is written to a known entity.
    EntityId Body;
    EntityId Camera;
};
