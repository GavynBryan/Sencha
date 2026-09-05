#include <camera/CameraRegistration.h>

#include <camera/CameraExclusion.h>
#include <ecs/World.h>

void RegisterCameraComponents(World& world)
{
    if (!world.IsRegistered<CameraExclusion>())
        world.RegisterComponent<CameraExclusion>();
}
