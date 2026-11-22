#pragma once

#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>
#include <zone/ZoneStateLists.h>
#include <zone/ZoneStateScope.h>

#include <functional>
#include <span>
#include <vector>

class ComponentSerializerRegistry;
class World;
struct SceneSerializationContext;

// Reads only; held records are settled first (SettleHeldRecords). What the world
// and `roots` hold, and what waits for a holder that is no level entity.
[[nodiscard]] JsonValue CaptureHeldRecords(const World& world, std::span<const EntityId> roots,
                                           const ComponentSerializerRegistry& serializers,
                                           SceneSerializationContext& context);

void RestoreHeldRecords(World& world, const JsonValue& data, const ComponentSerializerRegistry& serializers,
                        SceneSerializationContext& context);

struct ScopedHeldRecords
{
    ZoneStateScope Scope;
    std::vector<ZoneListEntity> Records;
};

// What level entities hold, resident or waiting, by the zone each holder belongs
// to; `scopeOf` names a resident holder's.
[[nodiscard]] std::vector<ScopedHeldRecords> CaptureLevelHeldRecords(
    const World& world, const std::function<ZoneStateScope(EntityId)>& scopeOf,
    const ComponentSerializerRegistry& serializers, SceneSerializationContext& context);

// Each record waits for its holder under `scope`.
void RestoreLevelHeldRecords(World& world, ZoneStateScope scope, std::span<const ZoneListEntity> records,
                             const ComponentSerializerRegistry& serializers, SceneSerializationContext& context);
