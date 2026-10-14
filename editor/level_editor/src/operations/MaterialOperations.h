#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// material.apply, which paints brush faces with one of the project's
// materials. `level` must outlive their registration.
[[nodiscard]] std::vector<EditorOperationEntry> MaterialOperations(LevelTarget& level);
