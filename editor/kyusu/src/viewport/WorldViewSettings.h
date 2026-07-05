#pragma once

#include <math/Vec.h>
#include <zone/ZoneId.h>

#include <string>

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
    // Proximity demand preview: mirrors streaming_radius. 0 = graph only.
    float PreviewRadius = 0.0f;
    // Sticky preview focus and the camera position it resolved from, updated
    // per frame while the preview is on. Session transients: never persisted.
    ZoneId PreviewFocus;
    Vec3d  PreviewFocusPosition{};
    // Scratch world tags (comma separated) for previewing gated connections;
    // mirrors what the game would push through SetWorldTags. Transient.
    std::string PreviewTags;
};
