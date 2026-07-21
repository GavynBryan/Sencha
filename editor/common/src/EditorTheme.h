#pragma once

#include <math/Vec.h>

// One place for the editor overlay's visual language — colors and screen-pixel
// sizes — so gizmos, handles, selection highlights, and component visuals read as
// one consistent UI instead of scattered literals. Pure data; included by both
// the manipulators (editmodes) and the renderers. (hardening-and-consolidation.md
// W2/W6 ethos; select-tool-v2 P4.)
namespace EditorTheme
{
// Axis colors (X/Y/Z = red/green/blue).
inline constexpr Vec4 AxisX{ 1.0f, 0.28f, 0.28f, 1.0f };
inline constexpr Vec4 AxisY{ 0.35f, 1.0f, 0.4f, 1.0f };
inline constexpr Vec4 AxisZ{ 0.4f, 0.55f, 1.0f, 1.0f };

// The part under the cursor brightens to this before a click.
inline constexpr Vec4 Hover{ 1.0f, 0.9f, 0.25f, 1.0f };

inline constexpr Vec4 Selection{ 1.0f, 0.6f, 0.1f, 1.0f };      // object outline (amber)
inline constexpr Vec4 Handle{ 1.0f, 1.0f, 1.0f, 1.0f };         // bounds resize squares
inline constexpr Vec4 BoundsBox{ 0.55f, 0.6f, 0.7f, 1.0f };     // bounds box edges
inline constexpr Vec4 FaceHighlight{ 1.0f, 0.85f, 0.1f, 1.0f };  // selected-face outline (yellow)
inline constexpr Vec4 FaceFill{ 1.0f, 0.92f, 0.45f, 0.22f };     // selected-face translucent fill (faint yellow)
inline constexpr Vec4 EdgeHighlight{ 1.0f, 0.85f, 0.1f, 1.0f };  // selected-edge stroke (yellow)
inline constexpr Vec4 VertexHighlight{ 1.0f, 1.0f, 1.0f, 1.0f };
inline constexpr Vec4 ComponentVisual{ 0.7f, 0.85f, 1.0f, 1.0f };
// Context zones (open but not the focus zone) render through the same solid,
// wireframe, and component-visual paths with their colors modulated by this,
// so they read as present but inert.
inline constexpr Vec4 ContextZoneDim{ 0.45f, 0.45f, 0.50f, 1.0f };
// Solid-view context zones instead render their materials full-bright with
// this translucent grey washed over the brush faces (the FaceFill mechanism),
// faint enough that the underlying textures stay readable.
inline constexpr Vec4 ContextZoneOverlay{ 0.55f, 0.55f, 0.60f, 0.30f };
inline constexpr Vec4 SolidWireframe{ 0.0f, 0.0f, 0.0f, 1.0f }; // face edges over solid body
// Transition graph edges in the streaming preview: cyan lines between zone centers.
inline constexpr Vec4 TransitionLine{ 0.25f, 0.75f, 0.95f, 1.0f };
// Irradiance probe volumes: the authored bounds box, and the lattice points a
// selected volume's bake will place (violet, distinct from zone/component blues).
inline constexpr Vec4 IrradianceVolume{ 0.72f, 0.45f, 0.95f, 1.0f };
inline constexpr Vec4 IrradianceProbePoint{ 0.85f, 0.68f, 1.0f, 1.0f };
// Streaming preview: zone bounds tinted by the live demand state around the
// preview focus (which keeps the Selection accent).
inline constexpr Vec4 PreviewDemanded{ 0.3f, 0.9f, 0.4f, 1.0f };
inline constexpr Vec4 PreviewUndemanded{ 0.65f, 0.2f, 0.2f, 1.0f };

inline constexpr Vec4 DimensionLabel{ 0.85f, 0.9f, 1.0f, 1.0f }; // selected-brush W/L/H text
inline constexpr Vec4 Readout{ 0.3f, 0.6f, 1.0f, 1.0f };         // drag origin->current line + distance
// The active selection's mesh edges: a vivid blue authored above 1.0 so the bloom pass
// (editor.bloom.*) extracts the bright excess into a glow. Exceeding 1.0 is intended
// now: it renders into the RGBA16F viewport target, and only the > threshold part blooms
// while the displayed core clamps to a saturated blue.
inline constexpr Vec4 ActiveWireframe{ 0.25f, 0.55f, 2.5f, 1.0f };
// Preview mesh edges: a brush a click would make active (edge-cut hover, or another
// mesh hovered in an element mode). A dimmer, subdued blue, no glow and no handles,
// so it stays distinct from (and subordinate to) the active body.
inline constexpr Vec4 PreviewWireframe{ 0.1f, 0.3f, 0.62f, 1.0f };
inline constexpr Vec4 HoverEligible{ 0.4f, 1.0f, 0.85f, 1.0f };  // element under the cursor (selection-eligible)
inline constexpr Vec4 VertexHandle{ 0.6f, 0.7f, 0.85f, 1.0f };   // all vertices shown in vertex mode
// Soft (smooth-shaded) edges: a green stroke replacing the wireframe color on
// edges marked soft, so softness reads directly in the viewport.
inline constexpr Vec4 SoftEdgeWireframe{ 0.35f, 0.95f, 0.5f, 1.0f };

// Screen-constant sizes (pixels), resolved to world via ViewportProjection.
inline constexpr float GizmoAxisPixels = 90.0f;
inline constexpr float VertexDotPixels = 7.0f;
inline constexpr float HandlePixels = 8.0f;

// Overlay line stroke widths (pixels), resolved to a screen-constant width by the
// wide-line pipeline. The active body reads bolder than the preview/hover strokes.
inline constexpr float ActiveLinePixels = 2.0f;   // active-body wireframe
inline constexpr float OverlayLinePixels = 1.5f;  // hover/element highlights, manipulators
inline constexpr float PreviewLinePixels = 1.0f;  // preview-body wireframe (subordinate)
} // namespace EditorTheme
