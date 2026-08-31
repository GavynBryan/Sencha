#include "CubeDemoScene.h"

#include <components/ActiveCameraService.h>
#include <core/logging/LoggingProvider.h>
#include <world/RuntimeWorld.h>

#include <algorithm>
#include <vector>

bool FinalizeDemoScene(
    DemoScene& scene,
    RuntimeWorld& runtime,
    RuntimeZoneRecord& zone,
    LoggingProvider& logging,
    FreeCamera& freeCamera)
{
    Logger& log = logging.GetLogger<DemoScene>();

    std::vector<EntityId> entities;
    for (EntityId entity : runtime.Entities().GetAliveEntities())
    {
        if (runtime.Entities().GetEntityPartition(entity)
            == zone.Partition)
        {
            entities.push_back(entity);
        }
    }
    std::sort(
        entities.begin(),
        entities.end(),
        [](EntityId left, EntityId right)
        {
            return left.Index < right.Index;
        });

    if (entities.size() < 3)
    {
        log.Error(
            "CubeDemo: imported scene has {} entities; expected at least 3",
            entities.size());
        return false;
    }

    scene.Camera = entities[0];
    scene.CenterCube = entities[1];
    scene.CenterCubeChild = entities[2];

    runtime.Entities()
        .GetResource<ActiveCameraService>()
        .SetActive(scene.Camera);
    freeCamera.Entity = scene.Camera;
    return true;
}
