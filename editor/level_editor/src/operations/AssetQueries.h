#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// assets.list: the level's project content, found as the asset shelf finds it.
// `level` must outlive their registration.
[[nodiscard]] std::vector<EditorOperationEntry> AssetQueries(const LevelTarget& level);
