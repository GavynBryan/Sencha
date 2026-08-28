#include "CubeDemoScene.h"

#include <components/ActiveCameraService.h>
#include <core/logging/LoggingProvider.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <algorithm>
#include <vector>

bool BuildDemoScenePackage(
    EntityBuildPackage& package,
    std::string_view scenePath,
    const ComponentSerializerRegistry& serializers,
    std::string* error)
{
    SmapContents contents;
    SmapError smapError;
    if (!ReadSmapFile(std::string(scenePath), serializers, contents, &smapError)
        || !BuildEntityPackageFromSmap(contents, serializers, package, &smapError))
    {
        if (error != nullptr)
            *error = smapError.Message;
        return false;
    }
    return true;
}

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
