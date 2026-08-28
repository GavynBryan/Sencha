#include "CubeDemoScene.h"

#include <components/ActiveCameraService.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/build/EntityBuildPackage.h>
#include <zone/ZonePackageSceneLoader.h>

#include <algorithm>
#include <cassert>
#include <format>
#include <fstream>
#include <sstream>
#include <vector>

DemoSceneParse ParseDemoSceneFile(std::string_view scenePath)
{
    DemoSceneParse result;

    std::ifstream file{ std::string(scenePath) };
    if (!file.is_open())
    {
        result.Error = std::format(
            "could not open scene file '{}'",
            scenePath);
        return result;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    result.Json = JsonParse(buffer.str(), &parseError);
    if (!result.Json)
    {
        result.Error = std::format(
            "scene JSON parse error at {}: {}",
            parseError.Position,
            parseError.Message);
    }
    return result;
}

bool BuildDemoScenePackage(
    EntityBuildPackage& package,
    const DemoSceneParse& parsed,
    const ComponentSerializerRegistry& serializers,
    SceneLoadError* error)
{
    if (!parsed.Json)
    {
        if (error != nullptr)
            error->Message = parsed.Error;
        return false;
    }

    return BuildEntityPackageFromSceneJson(
        *parsed.Json,
        serializers,
        package,
        error);
}

bool FinalizeDemoScene(
    DemoScene& scene,
    RuntimeWorld& runtime,
    RuntimeZoneRecord& zone,
    const DemoSceneParse& parsed,
    LoggingProvider& logging,
    FreeCamera& freeCamera)
{
    Logger& log = logging.GetLogger<DemoScene>();
    if (!parsed.Json)
    {
        log.Error("CubeDemo: {}", parsed.Error);
        return false;
    }

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
