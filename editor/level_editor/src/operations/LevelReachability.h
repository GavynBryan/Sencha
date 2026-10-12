#pragma once

#include "operations/EditorOperations.h"

class WorldDocument;

// nav.reachable over the world document's focus level: the navigation is built
// in memory from the settings under the world's first content root, and the
// question is asked twice, unknown traversal kinds first blocked and then open
// (docs/plans/level-spatial-model.md, section 6.4).
[[nodiscard]] EditorOperationEntry ReachabilityOperation(const WorldDocument& world);
