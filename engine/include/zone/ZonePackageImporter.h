#pragma once

#include <zone/ZoneParticipation.h>

#include <string>

class ComponentSerializerRegistry;
class RuntimeWorld;
struct SceneSerializationContext;
class WorldComponentSchema;
class ZoneLoadPackage;

struct ZoneImportError
{
    std::string Message;
};

// Owner-thread semantic kernel for publishing a detached package into a hidden
// RuntimeWorld partition. Any failure cancels the import completely; ordinary
// frame-domain systems can observe only the final published zone.
//
// The first overload imports package-native runtime bytes. The second also
// decodes worker-parsed serialized payloads through the engine-owned serializer
// registry and explicit scene context.
[[nodiscard]] bool ImportZonePackage(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    ZoneParticipation participation = {},
    ZoneImportError* error = nullptr);

[[nodiscard]] bool ImportZonePackage(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& sceneContext,
    ZoneParticipation participation = {},
    ZoneImportError* error = nullptr);
