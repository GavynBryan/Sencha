#pragma once

#include <core/json/JsonValue.h>
#include <world/serialization/SceneSerializer.h>

class ComponentSerializerRegistry;
class ZoneLoadPackage;

// Converts an already-parsed cooked scene into detached package-local entities
// and serialized component payloads. Safe on a worker after component
// registration is frozen: it performs no asset resolution and touches no World.
[[nodiscard]] bool BuildZonePackageFromSceneJson(
    const JsonValue& root,
    const ComponentSerializerRegistry& serializers,
    ZoneLoadPackage& package,
    SceneLoadError* error = nullptr);
