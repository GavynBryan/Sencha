#pragma once

#include "operations/EditorOperations.h"

class LevelTarget;

// space.graph: which spaces the walkable surface joins under a scenario, and
// whether it crosses each opening, measured on the level's navigation.
[[nodiscard]] EditorOperationEntry SpaceGraphOperation(const LevelTarget& level);
