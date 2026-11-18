#pragma once

#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>

#include <span>

class ComponentSerializerRegistry;
class World;
struct SceneSerializationContext;

// docs/gameplay/saves.md, "Held records". Gives a record or holder without a
// persistent id one.
[[nodiscard]] JsonValue CaptureHeldRecords(World& world, std::span<const EntityId> roots,
                                           const ComponentSerializerRegistry& serializers,
                                           SceneSerializationContext& context);

// Destroys every held record, then recreates the saved ones in the persistent
// partition; a holder not yet resident is resolved when it arrives.
void RestoreHeldRecords(World& world, const JsonValue& data, const ComponentSerializerRegistry& serializers,
                        SceneSerializationContext& context);
