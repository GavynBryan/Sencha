#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// changes.mark and changes.since: what changed in the level (never a draft)
// since a client last looked, by per-entity digest. The marks are held by the
// executors and bounded, so an old mark expires.
[[nodiscard]] std::vector<EditorOperationEntry> LevelChangeOperations(const LevelTarget& level);
