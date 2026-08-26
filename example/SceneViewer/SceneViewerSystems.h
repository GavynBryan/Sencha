#pragma once

#include "FreeCamera.h"

#include <app/EngineSchedule.h>

// Registers the viewer's camera systems and their ordering. Split out of
// SceneViewerGame so a test can compose the schedule -- and so exercise the
// After edges -- without a window, a device, or an engine.
//
// RegisterInputSystems must have run on the same schedule first: the ordering
// edge below names InputActionResolveSystem, and After asserts on an endpoint
// that is not registered.
void RegisterSceneViewerSystems(
    EngineSchedule& schedule,
    FreeCamera& freeCamera,
    bool& scriptedCameraEnabled);
