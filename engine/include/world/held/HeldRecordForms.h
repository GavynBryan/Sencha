#pragma once

#include <core/identity/Id.h>
#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <zone/ZoneStateLists.h>

#include <span>
#include <vector>

class ComponentSerializerRegistry;
class HeldRecordIndex;
class World;
struct SceneSerializationContext;

// A held record in scene form, written and made again: docs/gameplay/held-records.md.

// `records` and what they hold, to the nesting bound, in breadth order; records content authored are not listed.
[[nodiscard]] std::vector<EntityId> HeldRecordClosure(const World& world, const HeldRecordIndex& index,
                                                      std::vector<EntityId> records);

// Each record's id and its entity record; one without an id is logged and left out.
[[nodiscard]] std::vector<ZoneListEntity> WriteHeldRecords(const World& world, std::span<const EntityId> records,
                                                           const ComponentSerializerRegistry& serializers,
                                                           SceneSerializationContext& context);

// Invalid when a live entity has the id, when the record does not read whole, or when it holds no Held.
[[nodiscard]] EntityId MakeHeldRecord(World& world, StoragePartitionId partition, const ZoneListEntity& saved,
                                      const ComponentSerializerRegistry& serializers,
                                      SceneSerializationContext& context);
