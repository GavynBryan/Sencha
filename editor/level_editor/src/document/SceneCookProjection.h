#pragma once

#include "TriggerSensorCook.h"

#include <core/json/JsonValue.h>
#include <span>
#include <unordered_map>

class EditorDocument;
class LoggingProvider;
class AssetSystem;

[[nodiscard]] JsonValue BuildRuntimeSceneProjection(const EditorDocument& document, LoggingProvider& logging,
    AssetSystem* assets, std::span<const CookTriggerSensor> sensors, std::span<const EntityId> referencedEntities,
    std::unordered_map<EntityId, std::uint32_t, EntityIdHash>& sceneIndices);
