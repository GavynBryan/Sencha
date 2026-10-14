#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// draft.begin, write, list, diff, accept, discard, resume, rebase, save and
// open over `level`'s drafts; rebase and open replay through `operations`.
// Both must outlive their registration.
[[nodiscard]] std::vector<EditorOperationEntry> LevelDraftOperations(LevelTarget& level,
                                                                     EditorOperations& operations);
