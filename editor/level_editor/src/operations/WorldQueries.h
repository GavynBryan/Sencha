#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class WorldDocument;

// The read-only operations over the world as a whole: world.summary (its zones
// and the docks between them) and diagnostics (what the world's validation
// reports). The executors read `world` on each call, so it must outlive their
// registration.
[[nodiscard]] std::vector<EditorOperationEntry> WorldQueryOperations(const WorldDocument& world);

// The world's queries and world.focus, as the hosts register them.
[[nodiscard]] std::vector<EditorOperationEntry> WorldOperations(WorldDocument& world);
