#pragma once

#include "CubeDemoScene.h"
#include "FreeCamera.h"

#include <app/EngineSchedule.h>
#include <world/registry/Registry.h>

class CaptionRuntime;

void RegisterCubeDemoSystems(EngineSchedule& schedule,
                             Registry*& registry,
                             FreeCamera& freeCamera,
                             DemoScene& scene,
                             CaptionRuntime* captions);
