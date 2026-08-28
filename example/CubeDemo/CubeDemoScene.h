#pragma once

#include "FreeCamera.h"

#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>

#include <optional>
#include <string>
#include <string_view>

class ComponentSerializerRegistry;
class LoggingProvider;
class RuntimeWorld;
struct RuntimeZoneRecord;
class EntityBuildPackage;
struct SceneLoadError;

struct DemoScene
{
    EntityId Camera;
    EntityId CenterCube;
    EntityId CenterCubeChild;
};

struct DemoSceneParse
{
    std::optional<JsonValue> Json;
    std::string Error;
};

DemoSceneParse ParseDemoSceneFile(std::string_view scenePath);

bool BuildDemoScenePackage(
    EntityBuildPackage& package,
    const DemoSceneParse& parsed,
    const ComponentSerializerRegistry& serializers,
    SceneLoadError* error = nullptr);

bool FinalizeDemoScene(
    DemoScene& scene,
    RuntimeWorld& runtime,
    RuntimeZoneRecord& zone,
    const DemoSceneParse& parsed,
    LoggingProvider& logging,
    FreeCamera& freeCamera);
