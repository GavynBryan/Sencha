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
};

// The demo scene loads in two stages so the zone can load asynchronously
// (docs/ecs/parallelization.md, Decision 3). Assets are file-based: the
// game scans the assets directory at startup, and the scene deserializer
// loads .smesh/.smat/PNG content through AssetSystem on demand
// (docs/assets/pipeline.md, Stage 1).
//
//   1. ParseDemoSceneFile — the async work stage: file IO + JSON parse only.
//      Touches no engine state, so it is safe on a task thread; errors are
//      returned, not logged, because logger resolution is main-thread-only.
//   2. FinalizeDemoScene — main thread, inside the zone-load commit. Runs the
//      scene deserializer (which loads and acquires from the asset caches)
//      and wires camera/game state. Returns false (and logs) on failure.

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
