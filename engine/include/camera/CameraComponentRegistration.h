#pragma once

#include <camera/CameraBob.h>
#include <camera/CameraExclusion.h>
#include <camera/CameraSubject.h>
#include <camera/CameraTilt.h>
#include <components/CameraComponent.h>
#include <world/ComponentRegistrar.h>

// The authored camera an entity carries, what a camera in use leaves out of
// its picture, whose view it presents, and how that view bobs and tilts.
using CameraComponents = ComponentSet<CameraComponent, CameraExclusion, CameraSubject, CameraBob, CameraTilt>;

inline void RegisterCameraComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<CameraComponents>();
}
