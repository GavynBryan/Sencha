#pragma once

#include <math/Vec.h>
#include <zone/ZoneDemand.h>
#include <zone/ZoneId.h>
#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <render/RenderDebugView.h>
#endif

#include <optional>
#include <string>

// World-mode view state (the GridSettings pattern): never undoable, never in
// the .sworld, persisted per user in the world's sidecar.
struct WorldViewSettings
{
    bool ShowZoneBounds = true;
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    // Session-only override for Solid viewports. None follows the production
    // forward pipeline; every other value substitutes the diagnostic shader.
    RenderDebugView DebugViewMode = RenderDebugView::None;
#endif

    // Live demand visualization from the pure streaming policy: zone bounds
    // tint by demand state around a preview focus resolved from the
    // perspective viewport's camera, no cook or play session involved.
    bool StreamingPreview = false;
    // Explicit per-field preview overrides over the resolved per-graph shape
    // (absent = inherit, the manifest's own model). Never seeded from the
    // resolved config: a re-seed on graph change would clobber the user's
    // tweak, and a one-time seed decays into an absolute knob that hides the
    // authored shape.
    std::optional<int>   PreviewHopCount;
    std::optional<float> PreviewRadius;
    std::optional<int>   PreviewResidentCap;
    // Sticky preview focus and the camera position it resolved from, updated
    // per frame while the preview is on. Session transients: never persisted.
    ZoneId PreviewFocus;
    Vec3d  PreviewFocusPosition{};
    // Scratch world tags (comma separated) for previewing gated connections;
    // mirrors what the game would push through SetWorldTags. Transient.
    std::string PreviewTags;
};

// The preview's config in force: the focus zone's per-graph shape resolved
// over the engine defaults, with any explicit per-field preview overrides
// applied on top. Both preview consumers (demand list, bounds tint) resolve
// through this so they always show the same shape.
[[nodiscard]] WorldPartitionStreamingConfig
ResolvePreviewStreamingConfig(const WorldPartitionManifest& manifest, ZoneId focus,
                              const WorldViewSettings& view);
