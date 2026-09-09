#pragma once

#include "ChromeGeometry.h"

#include <imgui.h>

// The chrome's painters: the handful of draw-list strokes every chrome surface
// (panel frame, application chassis, bars, controls) is assembled from. They
// draw exactly what they are given, in the colors they are given; the frame,
// chassis, and header files decide the geometry and the palette.
namespace EditorChrome
{
void FillChamfered(ImDrawList* dl, const ChamferPoly& poly, ImU32 color);
void StrokeChamfered(ImDrawList* dl, const ChamferPoly& poly, ImU32 color, float width);

// Lights the silhouette from the top-left: edges facing up or left take the
// highlight, edges facing down or right the shadow, so a flat fill reads as a
// raised plate. Pass a polygon inset by half `width` for crisp lines.
void BevelChamfered(ImDrawList* dl, const ChamferPoly& poly, ImU32 highlight, ImU32 shadow, float width);

// A soft illuminated edge: a wide faint stroke under a narrower brighter one.
void GlowChamfered(ImDrawList* dl, const ChamferPoly& poly, ImVec4 color, float alpha, float width);

void VerticalGradient(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 top, ImU32 bottom);

// The inside edge of a recessed well: shadow along the top and left, a faint
// highlight along the bottom and right, drawn just inside [mn, mx].
void InsetWell(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 shadow, ImU32 highlight, float width);

// An opaque metal ring `ring` wide around [mn, mx] with its outer corners
// chamfered: what covers the content's edge after the content is drawn.
// `outside` fills the corner cut-offs so the silhouette reads against the
// surface behind it.
void FrameRing(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float ring, float chamfer, ImU32 metal, ImU32 outside);
} // namespace EditorChrome
