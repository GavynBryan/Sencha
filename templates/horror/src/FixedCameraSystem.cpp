#include "FixedCameraSystem.h"

#include <app/Engine.h>
#include <app/GameContexts.h>
#include <app/LoadedLevel.h>
#include <camera/CameraQueries.h>
#include <components/ActiveCameraService.h>
#include <core/logging/Logger.h>
#include <ecs/World.h>
#include <world/RuntimeWorld.h>

void FixedCameraSystem::ZoneResidency(ZoneResidencyContext& ctx)
{
    if (Owner == nullptr)
        return;
    const ZoneId play = Owner->Level().PlayZone();
    if (!play.IsValid())
        return;

    World& world = ctx.Entities;
    for (const ZoneResidencyChange& change : ctx.Changes)
    {
        if (change.Zone != play)
            continue;
        ActiveCameraService& active = world.GetResource<ActiveCameraService>();
        if (change.Kind == ZoneResidencyChangeKind::Detaching)
        {
            active.SetActive(EntityId{});
            continue;
        }
        if (change.Kind != ZoneResidencyChangeKind::Attached)
            continue;

        const EntityId camera = FirstAuthoredCamera(world, change.Partition);
        active.SetActive(camera);
        if (Log != nullptr)
        {
            if (camera.IsValid())
                Log->Info("HorrorGame: looking through the room's authored camera");
            else
                Log->Warn("HorrorGame: the level authored no camera; nothing is looking");
        }
    }
}
