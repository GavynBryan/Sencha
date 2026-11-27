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

// Reads only, after SettleHeldRecords; leaves out what level entities hold.
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

// `scopeOf` names a resident holder's zone; a waiting record's is its own.
[[nodiscard]] std::vector<ScopedHeldRecords> CaptureLevelHeldRecords(
    const World& world, const std::function<ZoneStateScope(EntityId)>& scopeOf,
    const ComponentSerializerRegistry& serializers, SceneSerializationContext& context);
