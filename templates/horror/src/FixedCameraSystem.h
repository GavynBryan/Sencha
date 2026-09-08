#pragma once

#include <ecs/EntityId.h>

class Engine;
class Logger;
struct ZoneResidencyContext;

//=============================================================================
// FixedCameraSystem
//
// This game's camera policy: the level says where the camera is. When the play
// zone arrives, the first camera authored in it becomes the active one, and
// nothing follows anybody. Cutting between several authored cameras as the
// player moves is the obvious next step and deliberately not here: it wants a
// trigger mechanism the engine does not have yet.
//=============================================================================
struct FixedCameraSystem
{
    Engine* Owner = nullptr;
    Logger* Log = nullptr;

    void ZoneResidency(ZoneResidencyContext& ctx);
};
