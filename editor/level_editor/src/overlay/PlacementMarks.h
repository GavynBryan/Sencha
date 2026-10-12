#pragma once

class PlacementSession;
struct EditorOverlayState;

// The pending placement's cross and facing arrow (warning-coloured when
// refused), and an entity's marker, appended to the markers rebuilt just
// before this runs.
void AppendPlacementMarks(const PlacementSession& session, float gridSpacing, EditorOverlayState& overlay);
