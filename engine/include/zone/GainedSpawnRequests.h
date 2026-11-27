#pragma once

#include <core/assets/AssetId.h>
#include <core/identity/Id.h>
#include <core/json/JsonValue.h>
#include <ecs/StoragePartitionId.h>
#include <math/geometry/3d/Transform3d.h>
#include <zone/ZoneStateScope.h>

#include <span>
#include <vector>

// A gained member written from its source, made by the spawn service once its zone is attached:
// docs/gameplay/saves.md, "What a zone gained".
struct GainedSpawnRequest
{
    ZoneStateScope Scope;
    StoragePartitionId Partition;
    PersistentEntityId Id;
    AssetId Source;
    Transform3f Root = Transform3f::Identity();
    JsonValue Patch;
};

// World resource a zone's import fills and the engine's GainedMemberSpawns empties, so a capture
// keeps a member whose spawn has not landed and a record held by it waits for it.
class GainedSpawnRequests
{
public:
    void Add(GainedSpawnRequest request);
    void Remove(ZoneStateScope scope, PersistentEntityId id);
    // At its zone's detach: the store keeps what was not made yet.
    void ForgetZone(ZoneStateScope scope);
    void Clear() { Waiting.clear(); }

    [[nodiscard]] bool Awaits(ZoneStateScope scope, PersistentEntityId id) const;
    [[nodiscard]] bool AwaitsHolder(PersistentEntityId id) const;
    [[nodiscard]] std::span<const GainedSpawnRequest> Entries() const { return Waiting; }

private:
    std::vector<GainedSpawnRequest> Waiting;
};
