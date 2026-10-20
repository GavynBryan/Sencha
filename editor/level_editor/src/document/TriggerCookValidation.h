#pragma once

#include <string>
#include <vector>

class EditorDocument;
struct RuntimeAssets;

[[nodiscard]] bool ValidateTriggerProgramsForCook(
    const EditorDocument& document, RuntimeAssets* assets, std::vector<std::string>& errors);
