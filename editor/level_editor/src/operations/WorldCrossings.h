#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// Where walkable surface crosses from one zone into another and no dock says
// so (world.crossings), and a dock fitted to such a crossing or to an opening
// (dock.place_at). docs/plans/level-spatial-model.md, section 6.2.
[[nodiscard]] std::vector<EditorOperationEntry> WorldCrossingOperations(LevelTarget& level);
