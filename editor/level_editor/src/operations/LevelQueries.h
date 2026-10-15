#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// The read-only operations over `level`, which must outlive their registration:
// level.summary, entity.describe, query.box, query.near, plan.section,
// nav.reachable, measure.clearance and conventions.get.
[[nodiscard]] std::vector<EditorOperationEntry> LevelQueryOperations(const LevelTarget& level);
