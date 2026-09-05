#pragma once

#include <camera/CameraExclusion.h>
#include <components/CameraComponent.h>
#include <world/ComponentRegistrar.h>

// The authored camera an entity carries, and what a camera in use leaves out
// of its picture.
using CameraComponents = ComponentSet<CameraComponent, CameraExclusion>;

inline void RegisterCameraComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<CameraComponents>();
}
