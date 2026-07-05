#pragma once

#include <zone/WorldPartitionIds.h>

class WorldDocument;

// The connection's inline edit widgets (topology, priority, preload depth,
// required tags), routed through the WorldDocument verbs and applied to BOTH
// directions when a partner edge is supplied: a connection is one thing,
// however the streaming graph stores it. No-op when the id names no record.
void DrawTransitionInlineEditor(WorldDocument& world, TransitionId transition,
                                TransitionId partner = {});
