#pragma once

#include <ecs/StoragePartitionId.h>
#include <zone/ZoneParticipation.h>

#include <string>

class ComponentSerializerRegistry;
class RuntimeWorld;
struct SceneSerializationContext;
class World;
class WorldComponentSchema;
class ZoneLoadPackage;

struct ZoneImportError
{
    std::string Message;
};

// Imports detached package entities into an existing storage partition. On
// failure, only entities created by this call are destroyed. This is the common
// semantic kernel for persistent world-scene import and hidden zone import.
[[nodiscard]] bool ImportPackageIntoPartition(
    World& world,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    StoragePartitionId partition,
    ZoneImportError* error = nullptr);

[[nodiscard]] bool ImportPackageIntoPartition(
    World& world,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    StoragePartitionId partition,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& sceneContext,
    ZoneImportError* error = nullptr);

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
