#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelEditTarget;

// The operations that change a level, each committing one change set to
// `target` and answering with what it created, modified and deleted and the
// revisions either side. `target` must outlive their registration.
[[nodiscard]] std::vector<EditorOperationEntry> LevelMutationOperations(LevelEditTarget& target);
