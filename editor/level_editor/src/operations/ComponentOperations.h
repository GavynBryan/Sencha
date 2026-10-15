#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// component.set: one field of one component of an entity, written through
// the entity's serialized record, so any registered component can be edited
// without naming its type. `level` must outlive their registration.
[[nodiscard]] std::vector<EditorOperationEntry> ComponentOperations(LevelTarget& level);
