#include "ChromePaint.h"

#include <algorithm>

namespace EditorChrome
{
void FillChamfered(ImDrawList* dl, const ChamferPoly& poly, ImU32 color)
{
    if (poly.Count < 3)
        return;
    dl->AddConvexPolyFilled(poly.P, poly.Count, color);
}

void StrokeChamfered(ImDrawList* dl, const ChamferPoly& poly, ImU32 color, float width)
{
    if (poly.Count < 2)
        return;
    dl->AddPolyline(poly.P, poly.Count, color, ImDrawFlags_Closed, width);
}

void BevelChamfered(ImDrawList* dl, const ChamferPoly& poly, ImU32 highlight, ImU32 shadow, float width)
{
    if (poly.Count < 2)
        return;
    for (int i = 0; i < poly.Count; ++i)
    {
        const ImVec2 a = poly.P[i];
        const ImVec2 b = poly.P[(i + 1) % poly.Count];
        // Outward normal of a clockwise edge in screen space (y down). The
        // light sits slightly above the top-left, so a top-right diagonal
        // catches it and a bottom-left diagonal falls into shadow.
        const float nx = b.y - a.y;
        const float ny = -(b.x - a.x);
        const bool lit = nx * 0.9f + ny * 1.1f < 0.0f;
        dl->AddLine(a, b, lit ? highlight : shadow, width);
    }
}

void GlowChamfered(ImDrawList* dl, const ChamferPoly& poly, ImVec4 color, float alpha, float width)
{
    if (poly.Count < 2 || alpha <= 0.0f || width <= 0.0f)
        return;
    const ImU32 wide = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, alpha * 0.35f));
    const ImU32 core = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, alpha));
    dl->AddPolyline(poly.P, poly.Count, wide, ImDrawFlags_Closed, width);
    dl->AddPolyline(poly.P, poly.Count, core, ImDrawFlags_Closed, std::max(1.0f, width * 0.4f));
}

void DrawBracketCorners(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float length, ImU32 color, float width)
{
    const BracketLines lines = BracketCorners(mn, mx, length);
    for (int i = 0; i < lines.Count; i += 2)
        dl->AddLine(lines.Points[i], lines.Points[i + 1], color, width);
}

void VerticalGradient(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 top, ImU32 bottom)
{
    if (mx.x <= mn.x || mx.y <= mn.y)
        return;
    dl->AddRectFilledMultiColor(mn, mx, top, top, bottom, bottom);
}

void InsetWell(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 shadow, ImU32 highlight, float width)
{
    if (mx.x <= mn.x || mx.y <= mn.y || width <= 0.0f)
        return;
    const float h = width * 0.5f;
    dl->AddLine(ImVec2(mn.x, mn.y + h), ImVec2(mx.x, mn.y + h), shadow, width);
    dl->AddLine(ImVec2(mn.x + h, mn.y), ImVec2(mn.x + h, mx.y), shadow, width);
    dl->AddLine(ImVec2(mn.x, mx.y - h), ImVec2(mx.x, mx.y - h), highlight, width);
    dl->AddLine(ImVec2(mx.x - h, mn.y), ImVec2(mx.x - h, mx.y), highlight, width);
}

void FrameRing(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float ring, float chamfer, ImU32 metal, ImU32 outside)
{
    const float w = mx.x - mn.x;
    const float h = mx.y - mn.y;
    if (w <= 0.0f || h <= 0.0f || ring <= 0.0f)
        return;
    const float r = std::min(ring, std::min(w, h) * 0.5f);
    dl->AddRectFilled(mn, ImVec2(mx.x, mn.y + r), metal);
    dl->AddRectFilled(ImVec2(mn.x, mx.y - r), mx, metal);
    dl->AddRectFilled(ImVec2(mn.x, mn.y + r), ImVec2(mn.x + r, mx.y - r), metal);
    dl->AddRectFilled(ImVec2(mx.x - r, mn.y + r), mx, metal);

    const float c = std::clamp(chamfer, 0.0f, std::min(w, h) * 0.5f);
    if (c <= 0.0f)
        return;
    dl->AddTriangleFilled(mn, ImVec2(mn.x + c, mn.y), ImVec2(mn.x, mn.y + c), outside);
    dl->AddTriangleFilled(ImVec2(mx.x, mn.y), ImVec2(mx.x, mn.y + c), ImVec2(mx.x - c, mn.y), outside);
    dl->AddTriangleFilled(mx, ImVec2(mx.x - c, mx.y), ImVec2(mx.x, mx.y - c), outside);
    dl->AddTriangleFilled(ImVec2(mn.x, mx.y), ImVec2(mn.x, mx.y - c), ImVec2(mn.x + c, mx.y), outside);
}
} // namespace EditorChrome
