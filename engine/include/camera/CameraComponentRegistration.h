#pragma once

#include <camera/CameraExclusion.h>
#include <camera/CameraSubject.h>
#include <components/CameraComponent.h>
#include <world/ComponentRegistrar.h>

// The authored camera an entity carries, what a camera in use leaves out of
// its picture, and whose view it presents.
using CameraComponents = ComponentSet<CameraComponent, CameraExclusion, CameraSubject>;

inline void RegisterCameraComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<CameraComponents>();
}
