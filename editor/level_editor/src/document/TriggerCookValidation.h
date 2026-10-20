#pragma once

#include <string>
#include <vector>
#include <ecs/EntityId.h>

class EditorDocument;
struct RuntimeAssets;

[[nodiscard]] bool ValidateTriggerProgramsForCook(
    const EditorDocument& document, RuntimeAssets* assets, std::vector<std::string>& errors,
    std::vector<EntityId>* referencedEntities = nullptr);
