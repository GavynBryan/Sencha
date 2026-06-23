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
inline constexpr Vec4 FaceHighlight{ 1.0f, 0.45f, 0.12f, 1.0f };
inline constexpr Vec4 EdgeHighlight{ 0.2f, 0.9f, 1.0f, 1.0f };
inline constexpr Vec4 VertexHighlight{ 1.0f, 1.0f, 1.0f, 1.0f };
inline constexpr Vec4 ComponentVisual{ 0.7f, 0.85f, 1.0f, 1.0f };
inline constexpr Vec4 SolidWireframe{ 0.0f, 0.0f, 0.0f, 1.0f }; // face edges over solid body

// Screen-constant sizes (pixels), resolved to world via ViewportProjection.
inline constexpr float GizmoAxisPixels = 90.0f;
inline constexpr float VertexDotPixels = 7.0f;
inline constexpr float HandlePixels = 8.0f;
} // namespace EditorTheme
