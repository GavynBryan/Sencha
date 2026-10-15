#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// Travel across a world's zones under a scenario: world.reachable between two
// points and world.bypasses, the gated docks the sure walk gets past without
// opening. A zone is left only through an authored connection.
[[nodiscard]] std::vector<EditorOperationEntry> WorldReachabilityOperations(LevelTarget& level);
