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

// Imports into a hidden RuntimeWorld partition but does not publish it. Used by
// AsyncZoneLoader so owner-thread cache/backend/entity finalization can still
// fail and cancel atomically before an Attached residency change exists.
[[nodiscard]] bool ImportZonePackageHidden(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    ZoneImportError* error = nullptr);

[[nodiscard]] bool ImportZonePackageHidden(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& sceneContext,
    ZoneImportError* error = nullptr);

// Convenience wrappers for synchronous owner-thread callers that do not need a
// separate finalization step. They import hidden data and publish only after the
// entire package succeeds.
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
