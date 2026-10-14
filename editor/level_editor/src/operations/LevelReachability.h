#pragma once

#include "operations/EditorOperations.h"

class LevelTarget;

// nav.reachable over the target level: the navigation is built
// in memory from the settings under its first content root, and the
// question is asked twice, unknown traversal kinds first blocked and then open
// (docs/plans/level-spatial-model.md, section 6.4).
[[nodiscard]] EditorOperationEntry ReachabilityOperation(const LevelTarget& level);
