#pragma once

#include "FreeCamera.h"

#include <render/Material.h>
#include <render/MeshService.h>
#include <world/entity/EntityId.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformStore.h>

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

DemoScene CreateDemoScene(Registry& registry,
                          MeshService& meshes,
                          MaterialStore& materials,
                          FreeCamera& freeCamera);
