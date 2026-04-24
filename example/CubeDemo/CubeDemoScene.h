#pragma once

#include "FreeCamera.h"

#include <ecs/World.h>
#include <ecs/EntityId.h>
#include <render/Material.h>
#include <render/static_mesh/StaticMeshHandle.h>
#include <world/registry/Registry.h>

#include <string_view>

class AssetSystem;
class LoggingProvider;

struct DemoScene
{
    EntityId Camera;
    EntityId CenterCube;
    EntityId CenterCubeChild;
    StaticMeshHandle CubeMesh;
    MaterialHandle Red;
    MaterialHandle Green;
    MaterialHandle Blue;
};

DemoScene LoadDemoScene(Registry& registry,
                        AssetSystem& assets,
                        LoggingProvider& logging,
                        FreeCamera& freeCamera,
                        std::string_view scenePath);
