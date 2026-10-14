#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// Every operation over `level`: its queries, its mutations and its drafts,
// which replay through `operations`.
[[nodiscard]] std::vector<EditorOperationEntry> LevelOperations(LevelTarget& level, EditorOperations& operations);
