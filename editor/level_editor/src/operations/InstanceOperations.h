#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// instance.place: a scene source placed into the level as one undoable step,
// its entities projected from the placement. `level` must outlive their
// registration.
[[nodiscard]] std::vector<EditorOperationEntry> InstanceOperations(LevelTarget& level);
