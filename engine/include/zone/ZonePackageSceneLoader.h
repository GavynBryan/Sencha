#pragma once

#include <core/json/JsonValue.h>
#include <world/serialization/SceneSerializer.h>

class ComponentSerializerRegistry;
class EntityBuildPackage;

// Converts an already-parsed cooked scene into detached package-local entities
// and serialized component payloads. Safe on a worker after component
// registration is frozen: it performs no asset resolution and touches no World.
[[nodiscard]] bool BuildEntityPackageFromSceneJson(
    const JsonValue& root,
    const ComponentSerializerRegistry& serializers,
    EntityBuildPackage& package,
    SceneLoadError* error = nullptr);
