#pragma once

#include <camera/CameraRig.h>
#include <camera/CameraSeat.h>
#include <components/CameraComponent.h>
#include <world/ComponentRegistrar.h>

// The authored camera an entity carries, what a body's own camera says it is
// for, and the runtime rig that steers one.
inline void RegisterCameraComponents(ComponentRegistrar& registrar)
{
    registrar.Add<CameraComponent>();
    registrar.Add<CameraSeat>();
    registrar.Add<CameraRig>();
}
