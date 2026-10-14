#pragma once

#include "operations/EditorOperations.h"

class LevelTarget;

// measure.clearance over the target level: the floor, ceiling
// and free width around a point, measured by casting against the visible
// brushes, and set against the navigation profile's capsule when the project
// has navigation settings.
[[nodiscard]] EditorOperationEntry ClearanceOperation(const LevelTarget& level);
