#pragma once

#include "FreeCamera.h"

#include <ecs/EntityId.h>

class LoggingProvider;
class RuntimeWorld;
struct RuntimeZoneRecord;

struct DemoScene
{
    EntityId Camera;
    EntityId CenterCube;
    EntityId CenterCubeChild;
};

bool FinalizeDemoScene(
    DemoScene& scene,
    RuntimeWorld& runtime,
    RuntimeZoneRecord& zone,
    LoggingProvider& logging,
    FreeCamera& freeCamera);
