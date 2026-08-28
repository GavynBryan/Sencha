#pragma once

#include <ecs/StoragePartitionId.h>
#include <zone/ZoneId.h>
#include <zone/ZoneParticipation.h>

#include <string>

class ComponentSerializerRegistry;
class EntityBuildPackage;
class RuntimeWorld;
struct SceneSerializationContext;
class World;
class WorldComponentSchema;

struct ZoneImportError
{
    std::string Message;
};

// Imports detached package entities into an existing storage partition. On
// failure, only entities created by this call are destroyed.
//
// `stateScope` names the ZoneStateStore scope this import is subject to:
// entities it records destroyed are not re-created, and the authored set is
// recorded under it on success. An invalid scope means no persisted state
// applies and the package imports verbatim, which is what content built for a
// single import wants. It is a separate parameter from the partition because
// the two genuinely differ -- the persistent world scene imports into partition
// zero under a zone's saved state.
//
// This is the common semantic kernel for persistent world-scene import and
// hidden zone import.
[[nodiscard]] bool ImportPackageIntoPartition(
    World& world,
    const WorldComponentSchema& schema,
    const EntityBuildPackage& package,
    StoragePartitionId partition,
    ZoneId stateScope,
    ZoneImportError* error = nullptr);

[[nodiscard]] bool ImportPackageIntoPartition(
    World& world,
    const WorldComponentSchema& schema,
    const EntityBuildPackage& package,
    StoragePartitionId partition,
    ZoneId stateScope,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& sceneContext,
    ZoneImportError* error = nullptr);

// Imports into a hidden RuntimeWorld partition but does not publish it. Used by
// AsyncZoneLoader so owner-thread cache/backend/entity finalization can still
// fail and cancel atomically before an Attached residency change exists.
[[nodiscard]] bool ImportZonePackageHidden(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    ZoneId zone,
    const EntityBuildPackage& package,
    ZoneImportError* error = nullptr);

[[nodiscard]] bool ImportZonePackageHidden(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    ZoneId zone,
    const EntityBuildPackage& package,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& sceneContext,
    ZoneImportError* error = nullptr);

[[nodiscard]] bool ImportZonePackage(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    ZoneId zone,
    const EntityBuildPackage& package,
    ZoneParticipation participation = {},
    ZoneImportError* error = nullptr);

[[nodiscard]] bool ImportZonePackage(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    ZoneId zone,
    const EntityBuildPackage& package,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& sceneContext,
    ZoneParticipation participation = {},
    ZoneImportError* error = nullptr);
