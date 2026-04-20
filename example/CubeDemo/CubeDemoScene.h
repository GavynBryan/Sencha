#pragma once

#include "FreeCamera.h"

#include <render/Material.h>
#include <render/MeshTypes.h>
#include <world/entity/EntityId.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformStore.h>

#include <string_view>

class AssetSystem;

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
                        AssetSystem& assets,
                        FreeCamera& freeCamera,
                        std::string_view scenePath);
