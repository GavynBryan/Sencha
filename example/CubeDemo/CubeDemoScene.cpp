#include "CubeDemoScene.h"

#include <core/assets/AssetSystem.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <render/Camera.h>
#include <render/StaticMeshComponent.h>
#include <world/serialization/SceneSerializer.h>
#include <zone/DefaultZoneBuilder.h>

#include <cassert>
#include <format>
#include <fstream>
#include <sstream>

DemoSceneParse ParseDemoSceneFile(std::string_view scenePath)
{
    DemoSceneParse result;

    std::ifstream file{ std::string(scenePath) };
    if (!file.is_open())
    {
        result.Error = std::format("could not open scene file '{}'", scenePath);
        return result;
    }

    std::ostringstream buf;
    buf << file.rdbuf();

    JsonParseError parseError;
    result.Json = JsonParse(buf.str(), &parseError);
    if (!result.Json)
    {
        result.Error = std::format("scene JSON parse error at {}: {}",
                                   parseError.Position, parseError.Message);
    }
    return result;
}

bool FinalizeDemoScene(DemoScene& scene,
                       Registry& registry,
                       const DemoSceneParse& parsed,
                       AssetSystem& assets,
                       LoggingProvider& logging,
                       FreeCamera& freeCamera)
{
    Logger& log = logging.GetLogger<DemoScene>();

    if (!parsed.Json)
    {
        log.Error("CubeDemo: {}", parsed.Error);
        assert(false && "Failed to read/parse demo scene file");
        return false;
    }

    SceneLoadError loadError;
    SceneSerializationContext sceneContext(logging, &assets);
    if (!LoadSceneJson(*parsed.Json, registry, sceneContext, &loadError))
    {
        log.Error("CubeDemo: scene load error: {}", loadError.Message);
        assert(false && "Failed to load demo scene");
        return false;
    }

    // Entities are loaded in JSON array order: 0=camera, 1=center cube, 2=center cube child.
    const auto entities = registry.Entities.GetAliveEntities();
    assert(entities.size() >= 3 && "Demo scene must have at least 3 entities");

    scene.Camera          = entities[0];
    scene.CenterCube      = entities[1];
    scene.CenterCubeChild = entities[2];

    registry.Resources.Get<ActiveCameraService>().SetActive(scene.Camera);
    freeCamera.Entity = scene.Camera;
    return true;
}
