#pragma once

#include "FreeCamera.h"

#include <render/MaterialCache.h>
#include <render/MeshCache.h>
#include <world/entity/EntityId.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformStore.h>

#include <string_view>

class LoggingProvider;

struct DemoScene
{
    EntityId Camera;
    EntityId CenterCube;
    EntityId CenterCubeChild;
    MeshHandle CubeMesh;
    MaterialHandle Red;
    MaterialHandle Green;
    MaterialHandle Blue;
};

TransformStore<Transform3f>& DemoTransforms(Registry& registry);

DemoScene LoadDemoScene(Registry& registry,
                        MeshCache& meshes,
                        MaterialCache& materials,
                        FreeCamera& freeCamera,
                        LoggingProvider& logging,
                        std::string_view scenePath);
