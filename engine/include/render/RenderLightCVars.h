#pragma once

class ConsoleRegistry;
struct RenderLightSet;

//=============================================================================
// Renderer tunables, one reader
//
// Applies the live `render.*` console tunables onto a frame's light set. Every
// host that renders a scene calls this, so a viewport, a material preview, and
// the game agree about exposure, tone mapping, ambient, and the baked-lighting
// toggles.
//
// It existed only inside the game pipeline before, and the editor re-read a
// subset with its own hard-coded fallbacks -- eight of the fifteen. Editor
// viewports therefore rendered untonemapped, at exposure 1.0, with no diffuse
// wrap or minimum-ambient floor, and ignored `render.baked_direct.enabled` and
// `render.ao.enabled` entirely. That is a WYSIWYG divergence in exactly the
// settings someone dials in while looking at a viewport.
//
// Fallbacks are the light set's own member defaults rather than repeated
// literals, so an unregistered or wrongly-typed cvar leaves the field alone.
//
// The debug view is deliberately not here: the game selects it from
// `render.debug.view` while the editor selects it from its own UI, so it has
// two legitimate sources and each host assigns it.
void ApplyRendererCVars(const ConsoleRegistry* console, RenderLightSet& lights);
