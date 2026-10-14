#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class LevelTarget;

// draft.begin, draft.write, draft.list, draft.diff, draft.accept,
// draft.discard, draft.resume and draft.rebase over `level`'s drafts. A rebase
// replays through `operations`. Both must outlive their registration.
[[nodiscard]] std::vector<EditorOperationEntry> LevelDraftOperations(LevelTarget& level,
                                                                     EditorOperations& operations);
