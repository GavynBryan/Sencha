#include "CubeDemoScene.h"

#include <core/assets/AssetSystem.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <render/Camera.h>
#include <render/StaticMeshComponent.h>
#include <render/static_mesh/StaticMeshPrimitives.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformHierarchyService.h>
#include <zone/DefaultZoneBuilder.h>

#include <cassert>
#include <fstream>
#include <sstream>

TransformStore<Transform3f>& DemoTransforms(Registry& registry)
{
    return registry.Components.Get<TransformStore<Transform3f>>();
}

DemoScene LoadDemoScene(Registry& registry,
                        AssetSystem& assets,
                        LoggingProvider& logging,
                        FreeCamera& freeCamera,
                        std::string_view scenePath)
{
    DemoScene scene;

    scene.CubeMesh = assets.RegisterProceduralStaticMesh(
        "asset://meshes/dev/cube.smesh",
        StaticMeshPrimitives::BuildCube(1.0f));
    scene.Red = assets.RegisterProceduralMaterial(
        "asset://materials/dev/red.smat",
        Material{ .Pass = ShaderPassId::ForwardOpaque, .BaseColor = Vec4(1.0f, 0.15f, 0.1f,  1.0f) });
    scene.Green = assets.RegisterProceduralMaterial(
        "asset://materials/dev/green.smat",
        Material{ .Pass = ShaderPassId::ForwardOpaque, .BaseColor = Vec4(0.1f, 0.85f, 0.45f, 1.0f) });
    scene.Blue = assets.RegisterProceduralMaterial(
        "asset://materials/dev/blue.smat",
        Material{ .Pass = ShaderPassId::ForwardOpaque, .BaseColor = Vec4(0.2f, 0.45f, 1.0f,  1.0f) });

    std::ifstream file{std::string(scenePath)};
    if (!file.is_open())
    {
        logging.GetLogger<DemoScene>().Error("CubeDemo: could not open scene file '{}'", scenePath);
        assert(false && "Failed to open demo scene file");
        return scene;
    }

    std::ostringstream buf;
    buf << file.rdbuf();

    JsonParseError parseError;
    auto json = JsonParse(buf.str(), &parseError);
    if (!json)
    {
        logging.GetLogger<DemoScene>().Error(
            "CubeDemo: scene JSON parse error at {}: {}",
            parseError.Position,
            parseError.Message);
        assert(false && "Failed to parse demo scene JSON");
        return scene;
    }

    SceneLoadError loadError;
    SceneSerializationContext sceneContext(logging, &assets);
    if (!LoadSceneJson(*json, registry, sceneContext, &loadError))
    {
        logging.GetLogger<DemoScene>().Error("CubeDemo: scene load error: {}", loadError.Message);
        assert(false && "Failed to load demo scene");
        return scene;
    }

    // Entities are loaded in JSON array order: 0=camera, 1=center cube, 2=center cube child.
    const auto entities = registry.Entities.GetAliveEntities();
    assert(entities.size() >= 3 && "Demo scene must have at least 3 entities");

    scene.Camera          = entities[0];
    scene.CenterCube      = entities[1];
    scene.CenterCubeChild = entities[2];

    registry.Resources.Get<ActiveCameraService>().SetActive(scene.Camera);
    freeCamera.Entity = scene.Camera;
    return scene;
}
