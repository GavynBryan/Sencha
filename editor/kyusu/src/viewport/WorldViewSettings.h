#pragma once

// World-mode view state (the GridSettings pattern): never undoable, never in
// the .sworld, persisted per user in the world's sidecar.
struct WorldViewSettings
{
    bool ShowZoneBounds = true;
};
