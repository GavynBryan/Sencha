#pragma once

#include <camera/CameraRig.h>
#include <components/CameraComponent.h>
#include <world/ComponentRegistrar.h>

// The authored camera an entity carries, and the runtime rig that steers one.
inline void RegisterCameraComponents(ComponentRegistrar& registrar)
{
    registrar.Add<CameraComponent>();
    registrar.Add<CameraRig>();
}
