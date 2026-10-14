#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// The operations that change a level, each one mutation of `target`, which
// must outlive their registration.
[[nodiscard]] std::vector<EditorOperationEntry> LevelMutationOperations(LevelTarget& target);
