#pragma once

#include "FreeCamera.h"

#include <ecs/EntityId.h>

#include <string>
#include <string_view>

class ComponentSerializerRegistry;
class LoggingProvider;
class RuntimeWorld;
struct RuntimeZoneRecord;
class EntityBuildPackage;

struct DemoScene
{
    EntityId Camera;
    EntityId CenterCube;
    EntityId CenterCubeChild;
};

// Reads the cooked demo scene and builds the zone package from it. On failure
// `error` carries the reason and the package is untouched.
bool BuildDemoScenePackage(
    EntityBuildPackage& package,
    std::string_view scenePath,
    const ComponentSerializerRegistry& serializers,
    std::string* error = nullptr);

bool FinalizeDemoScene(
    DemoScene& scene,
    RuntimeWorld& runtime,
    RuntimeZoneRecord& zone,
    LoggingProvider& logging,
    FreeCamera& freeCamera);
