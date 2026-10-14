#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// Every operation over `level`: its queries and its mutations.
[[nodiscard]] std::vector<EditorOperationEntry> LevelOperations(LevelTarget& level);
