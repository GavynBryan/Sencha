#include <camera/CameraRegistration.h>

#include <app/EngineSchedule.h>
#include <camera/CameraFollowSystem.h>
#include <camera/CameraRig.h>
#include <ecs/World.h>

void RegisterCameraComponents(World& world)
{
    if (!world.IsRegistered<CameraRig>())
        world.RegisterComponent<CameraRig>();
}

void RegisterCameraSystem(EngineSchedule& schedule)
{
    schedule.Register<CameraFollowSystem>();
}
