#pragma once

#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>

#include <span>

class ComponentSerializerRegistry;
class World;
struct SceneSerializationContext;

[[nodiscard]] JsonValue CaptureHeldRecords(World& world, std::span<const EntityId> roots,
                                           const ComponentSerializerRegistry& serializers,
                                           SceneSerializationContext& context);

void RestoreHeldRecords(World& world, const JsonValue& data, const ComponentSerializerRegistry& serializers,
                        SceneSerializationContext& context);
