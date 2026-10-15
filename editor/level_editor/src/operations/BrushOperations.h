#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// The operations on solid brushes: brush.box, and the edits that reshape a
// brush. `level` must outlive their registration.
[[nodiscard]] std::vector<EditorOperationEntry> BrushOperations(LevelTarget& level);
