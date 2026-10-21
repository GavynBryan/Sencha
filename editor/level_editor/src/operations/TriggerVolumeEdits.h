#pragma once

#include <ecs/EntityId.h>
#include <operations/EditorOperations.h>

#include <span>
#include <string>

class EditorDocument;
class EntityChangeRecorder;

[[nodiscard]] std::string TriggerVolumeConversionProblem(
    const EditorDocument& document, std::span<const EntityId> entities, bool toTrigger);
[[nodiscard]] EditorOperationOutcome ConvertTriggerVolumes(
    EditorDocument& document, EntityChangeRecorder& recorder, std::span<const EntityId> entities, bool toTrigger);
