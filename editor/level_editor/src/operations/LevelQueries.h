#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class WorldDocument;

// The read-only operations over the world document's focus level:
// level.summary, entity.describe, query.box, query.near and plan.section.
// The executors read `world` on each call, so it must outlive their
// registration.
[[nodiscard]] std::vector<EditorOperationEntry> LevelQueryOperations(const WorldDocument& world);
