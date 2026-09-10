#include "ChromeOrnaments.h"

#include "ui/EditorUiStyle.h"

#include <algorithm>
#include <cmath>

namespace
{
using namespace EditorChrome;

ImU32 Metal(float lighten) { return ImGui::GetColorU32(EditorUi::Lighten(EditorUi::MetalBase, lighten)); }
ImU32 Shadow() { return ImGui::GetColorU32(EditorUi::MetalShadow); }
ImU32 Highlight(float alpha) { return ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, alpha)); }
ImU32 Tinted(ImU32 tint, float alpha)
{
    const ImU32 a = static_cast<ImU32>(((tint >> 24) & 0xFFu) * alpha);
    return (tint & 0x00FFFFFFu) | (a << 24);
}

void Screw(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    const float r = std::min(mx.x - mn.x, mx.y - mn.y) * 0.5f;
    if (r < 1.0f)
        return;
    dl->AddCircleFilled(c, r, Metal(-0.15f));
    dl->AddCircle(c, r - 0.5f, Highlight(0.7f), 0, 1.0f);
    // The slot, lit by the accent so a mounting point reads as part of the
    // illuminated chassis rather than a dot.
    const float s = r * 0.6f;
    dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), Shadow(), 2.0f);
    dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), Tinted(tint, 0.7f), 1.0f);
}

void Vent(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    const float h = mx.y - mn.y;
    const float w = mx.x - mn.x;
    if (h < 2.0f || w < 4.0f)
        return;
    // Horizontal slots across the rect, each a dark cut with a lit lower lip.
    const int slots = std::max(1, static_cast<int>(h / 3.0f));
    const float pitch = h / static_cast<float>(slots);
    for (int i = 0; i < slots; ++i)
    {
        const float y = mn.y + pitch * static_cast<float>(i) + pitch * 0.25f;
        dl->AddRectFilled(ImVec2(mn.x, y), ImVec2(mx.x, y + std::max(1.0f, pitch * 0.4f)), Shadow());
        dl->AddLine(ImVec2(mn.x, y + std::max(1.0f, pitch * 0.4f) + 0.5f), ImVec2(mx.x, y + std::max(1.0f, pitch * 0.4f) + 0.5f),
                    Tinted(tint, 0.25f), 1.0f);
    }
}

void TripleSlash(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    const float h = mx.y - mn.y;
    const float w = mx.x - mn.x;
    if (h < 2.0f || w < 3.0f)
        return;
    const float step = w / 3.0f;
    const float lean = std::min(step * 0.6f, h);
    for (int i = 0; i < 3; ++i)
    {
        const float x = mn.x + step * static_cast<float>(i) + lean;
        dl->AddLine(ImVec2(x, mn.y), ImVec2(x - lean, mx.y), Tinted(tint, 0.8f), 1.0f);
    }
}

void StatusLed(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    const float r = std::min(mx.x - mn.x, mx.y - mn.y) * 0.5f;
    if (r < 1.0f)
        return;
    dl->AddCircleFilled(c, r, Tinted(tint, 0.25f));
    dl->AddCircleFilled(c, r * 0.55f, tint);
}

void Groove(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32)
{
    const float cy = std::floor((mn.y + mx.y) * 0.5f) + 0.5f;
    dl->AddLine(ImVec2(mn.x, cy), ImVec2(mx.x, cy), Shadow(), 1.0f);
    dl->AddLine(ImVec2(mn.x, cy + 1.0f), ImVec2(mx.x, cy + 1.0f), Highlight(0.5f), 1.0f);
}

void Seam(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32)
{
    const float cx = std::floor((mn.x + mx.x) * 0.5f) + 0.5f;
    dl->AddLine(ImVec2(cx, mn.y), ImVec2(cx, mx.y), Shadow(), 1.0f);
    dl->AddLine(ImVec2(cx + 1.0f, mn.y), ImVec2(cx + 1.0f, mx.y), Highlight(0.5f), 1.0f);
}

// A blueprint grid at a fixed design pitch, faint enough to sit behind copy.
void Grid(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    const float pitch = EditorUi::Px(24.0f);
    if (pitch < 4.0f || mx.x <= mn.x || mx.y <= mn.y)
        return;
    const ImU32 line = Tinted(tint, 0.35f);
    for (float x = std::floor(mn.x) + pitch; x < mx.x; x += pitch)
        dl->AddLine(ImVec2(x + 0.5f, mn.y), ImVec2(x + 0.5f, mx.y), line, 1.0f);
    for (float y = std::floor(mn.y) + pitch; y < mx.y; y += pitch)
        dl->AddLine(ImVec2(mn.x, y + 0.5f), ImVec2(mx.x, y + 0.5f), line, 1.0f);
}

}

namespace EditorChrome
{
void DrawOrnament(ImDrawList* dl, OrnamentKind kind, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    switch (kind)
    {
    case OrnamentKind::Screw:       Screw(dl, mn, mx, tint); break;
    case OrnamentKind::Vent:        Vent(dl, mn, mx, tint); break;
    case OrnamentKind::TripleSlash: TripleSlash(dl, mn, mx, tint); break;
    case OrnamentKind::StatusLed:   StatusLed(dl, mn, mx, tint); break;
    case OrnamentKind::Groove:      Groove(dl, mn, mx, tint); break;
    case OrnamentKind::Seam:        Seam(dl, mn, mx, tint); break;
    case OrnamentKind::Grid:        Grid(dl, mn, mx, tint); break;
    }
}
} // namespace EditorChrome
