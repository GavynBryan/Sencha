#pragma once

class EntityStore;
class EngineSchedule;

//=============================================================================
// Camera registration
//
// Opt-in camera follow: the CameraRig component plus the per-frame system that
// places the active camera from it. A game that drives its own camera skips both.
//=============================================================================

void RegisterCameraComponents(EntityStore& world);

void RegisterCameraSystem(EngineSchedule& schedule);
