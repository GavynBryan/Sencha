#pragma once

#include "CubeDemoScene.h"
#include "FreeCamera.h"

#include <app/EngineSchedule.h>

class CaptionRuntime;

void RegisterCubeDemoSystems(
    EngineSchedule& schedule,
    FreeCamera& freeCamera,
    DemoScene& scene,
    CaptionRuntime* captions);
