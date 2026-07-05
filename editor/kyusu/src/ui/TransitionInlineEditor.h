#pragma once

#include <zone/WorldPartitionIds.h>

class WorldDocument;

// The transition's inline edit widgets (topology combo, one-way checkbox,
// preload priority), routed through the WorldDocument verbs. Shared by the
// inspector's portal section and the World panel's connect bar so the edge
// edits identically everywhere. No-op when the id names no record.
void DrawTransitionInlineEditor(WorldDocument& world, TransitionId transition);
