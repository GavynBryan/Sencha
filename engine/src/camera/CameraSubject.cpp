#include <camera/CameraSubject.h>

#include <ecs/World.h>

void SetCameraSubject(World& world, EntityId camera, EntityId subject)
{
    if (!world.IsRegistered<CameraSubject>() || !world.IsAlive(camera))
        return;
    if (CameraSubject* current = world.TryGet<CameraSubject>(camera))
        current->Subject = subject;
    else
        world.AddComponent<CameraSubject>(camera, CameraSubject{ subject });
}
