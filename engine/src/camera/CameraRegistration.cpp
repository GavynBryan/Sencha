#include <camera/CameraRegistration.h>

#include <app/EngineSchedule.h>
#include <camera/CameraFollowSystem.h>
#include <camera/CameraRig.h>
#include <ecs/EntityStore.h>

void RegisterCameraComponents(EntityStore& world)
{
    if (!world.IsRegistered<CameraRig>())
        world.RegisterComponent<CameraRig>();
}

void RegisterCameraSystem(EngineSchedule& schedule)
{
    schedule.Register<CameraFollowSystem>();
}
