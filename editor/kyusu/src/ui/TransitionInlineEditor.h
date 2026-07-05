#pragma once

#include <zone/WorldPartitionIds.h>
#include <zone/WorldPartitionManifest.h>

class WorldDocument;

// The connection's inline edit widgets (topology, priority, preload depth,
// required tags), routed through the WorldDocument verbs and applied to BOTH
// directions when a partner edge is supplied: a connection is one thing,
// however the streaming graph stores it. No-op when the id names no record.
void DrawTransitionInlineEditor(WorldDocument& world, TransitionId transition,
                                TransitionId partner = {});

// What each topology value actually does today, stated plainly so the combo
// and the connection-row badge never read as a streaming control. Streaming
// shape is authored per region, never per topology.
[[nodiscard]] const char* TransitionTopologyHelp(TransitionTopology topology);
