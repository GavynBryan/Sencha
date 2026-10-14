#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// The operations on spaces, their shells and their openings
// (docs/plans/level-spatial-model.md, section 5). `level` must outlive their
// registration.
[[nodiscard]] std::vector<EditorOperationEntry> SpaceOperations(LevelTarget& level);
