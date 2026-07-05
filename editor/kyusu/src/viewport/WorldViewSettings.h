#pragma once

#include <zone/ZoneId.h>

// World-mode view state (the GridSettings pattern): never undoable, never in
// the .sworld, persisted per user in the world's sidecar.
struct WorldViewSettings
{
    bool ShowZoneBounds = true;

    // Live demand visualization from the pure streaming policy: zone bounds
    // tint by demand state around a preview focus resolved from the
    // perspective viewport's camera, no cook or play session involved.
    bool StreamingPreview = false;
    int  PreviewHopCount = 1;
    // Sticky preview focus, resolved per frame while the preview is on.
    // Session transient: never persisted.
    ZoneId PreviewFocus;
};
