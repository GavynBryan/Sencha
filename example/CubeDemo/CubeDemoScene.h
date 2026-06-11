#pragma once

#include "FreeCamera.h"

#include <core/json/JsonValue.h>
#include <ecs/World.h>
#include <ecs/EntityId.h>
#include <render/Material.h>
#include <render/static_mesh/StaticMeshHandle.h>
#include <world/registry/Registry.h>

#include <optional>
#include <string>
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

// The demo scene loads in three stages so the zone can load asynchronously
// (docs/ecs/parallelization.md, Decision 3):
//
//   1. RegisterDemoSceneAssets — main thread, before the load is submitted.
//      Registers procedural assets and stores their handles (plain values).
//   2. ParseDemoSceneFile — the async work stage: file IO + JSON parse only.
//      Touches no engine state, so it is safe on a task thread; errors are
//      returned, not logged, because logger resolution is main-thread-only.
//   3. FinalizeDemoScene — main thread, inside the zone-load commit. Runs the
//      scene deserializer (which acquires from the asset caches) and wires
//      camera/game state. Returns false (and logs) if stage 2 or 3 failed.

void RegisterDemoSceneAssets(DemoScene& scene, AssetSystem& assets);

struct DemoSceneParse
{
    std::optional<JsonValue> Json;
    std::string Error;
};

DemoSceneParse ParseDemoSceneFile(std::string_view scenePath);

bool FinalizeDemoScene(DemoScene& scene,
                       Registry& registry,
                       const DemoSceneParse& parsed,
                       AssetSystem& assets,
                       LoggingProvider& logging,
                       FreeCamera& freeCamera);
