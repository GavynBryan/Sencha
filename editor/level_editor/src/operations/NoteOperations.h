#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// Notes left in the level: note.add, note.resolve and notes.list. `level`
// must outlive their registration.
[[nodiscard]] std::vector<EditorOperationEntry> NoteOperations(LevelTarget& level);
