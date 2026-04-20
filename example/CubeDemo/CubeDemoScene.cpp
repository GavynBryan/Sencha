#include "CubeDemoScene.h"

#include <core/json/JsonParser.h>
#include <render/Camera.h>
#include <render/MeshRendererComponent.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformHierarchyService.h>
#include <zone/DefaultZoneBuilder.h>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>

TransformStore<Transform3f>& DemoTransforms(Registry& registry)
{
    return registry.Components.Get<TransformStore<Transform3f>>();
}

DemoScene LoadDemoScene(Registry& registry,
                        MeshCache& meshes,
                        MaterialCache& materials,
                        FreeCamera& freeCamera,
                        std::string_view scenePath)
{
    DemoScene scene;

    // Register assets in a fixed order so their handles match the JSON.
    // Mesh index 1 / Material indices 1-3 are baked into cube_demo_scene.json.
    scene.CubeMesh  = meshes.CreateFromData("cube_1m", MeshPrimitives::BuildCube(1.0f));
    scene.Red   = materials.Register("red",   Material{ .Pass = ShaderPassId::ForwardOpaque, .BaseColor = Vec4(1.0f, 0.15f, 0.1f,  1.0f) });
    scene.Green = materials.Register("green", Material{ .Pass = ShaderPassId::ForwardOpaque, .BaseColor = Vec4(0.1f, 0.85f, 0.45f, 1.0f) });
    scene.Blue  = materials.Register("blue",  Material{ .Pass = ShaderPassId::ForwardOpaque, .BaseColor = Vec4(0.2f, 0.45f, 1.0f,  1.0f) });

    std::ifstream file{std::string(scenePath)};
    if (!file.is_open())
    {
        std::fprintf(stderr, "CubeDemo: could not open scene file '%.*s'\n",
            static_cast<int>(scenePath.size()), scenePath.data());
        assert(false && "Failed to open demo scene file");
        return scene;
    }

    std::ostringstream buf;
    buf << file.rdbuf();

    JsonParseError parseError;
    auto json = JsonParse(buf.str(), &parseError);
    if (!json)
    {
        std::fprintf(stderr, "CubeDemo: scene JSON parse error at %zu: %s\n",
            parseError.Position, parseError.Message.c_str());
        assert(false && "Failed to parse demo scene JSON");
        return scene;
    }

    SceneLoadError loadError;
    if (!LoadSceneJson(*json, registry, &loadError))
    {
        std::fprintf(stderr, "CubeDemo: scene load error: %s\n", loadError.Message.c_str());
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
