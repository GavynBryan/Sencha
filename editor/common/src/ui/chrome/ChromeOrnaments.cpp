#include "ChromeOrnaments.h"

#include "ui/EditorUiStyle.h"

#include <algorithm>
#include <array>
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

void HazardStripe(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    const float h = mx.y - mn.y;
    const float w = mx.x - mn.x;
    if (h < 2.0f || w < 4.0f)
        return;
    dl->PushClipRect(mn, mx, true);
    const float pitch = std::max(4.0f, h);
    for (float x = mn.x - h; x < mx.x; x += pitch * 2.0f)
    {
        const ImVec2 p[4] = { ImVec2(x + h, mn.y), ImVec2(x + h + pitch, mn.y), ImVec2(x + pitch, mx.y), ImVec2(x, mx.y) };
        dl->AddConvexPolyFilled(p, 4, Tinted(tint, 0.55f));
    }
    dl->PopClipRect();
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

void CyanStrip(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    if (mx.x <= mn.x || mx.y <= mn.y)
        return;
    dl->AddRectFilled(mn, mx, Tinted(tint, 0.65f));
    dl->AddRect(ImVec2(mn.x - 1.0f, mn.y - 1.0f), ImVec2(mx.x + 1.0f, mx.y + 1.0f), Tinted(tint, 0.2f), 0.0f, 0, 2.0f);
}

void Scanlines(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    const ImU32 line = Tinted(tint, 0.06f);
    for (float y = mn.y + 0.5f; y < mx.y; y += 2.0f)
        dl->AddLine(ImVec2(mn.x, y), ImVec2(mx.x, y), line, 1.0f);
}

constexpr std::size_t kCount = static_cast<std::size_t>(OrnamentKind::Scanlines) + 1;

std::array<OrnamentSource, kCount> DefaultSources()
{
    std::array<OrnamentSource, kCount> t{};
    const auto row = [&](OrnamentKind kind, ProceduralGlyphFn fn) {
        t[static_cast<std::size_t>(kind)] = OrnamentSource{ GlyphSourceKind::Procedural, fn, {} };
    };
    row(OrnamentKind::Screw, Screw);
    row(OrnamentKind::Vent, Vent);
    row(OrnamentKind::TripleSlash, TripleSlash);
    row(OrnamentKind::HazardStripe, HazardStripe);
    row(OrnamentKind::StatusLed, StatusLed);
    row(OrnamentKind::Groove, Groove);
    row(OrnamentKind::Seam, Seam);
    row(OrnamentKind::CyanStrip, CyanStrip);
    row(OrnamentKind::Scanlines, Scanlines);
    return t;
}

std::array<OrnamentSource, kCount>& Sources()
{
    static std::array<OrnamentSource, kCount> sources = DefaultSources();
    return sources;
}
}

namespace EditorChrome
{
const OrnamentSource& OrnamentSourceFor(OrnamentKind kind)
{
    const std::size_t index = static_cast<std::size_t>(kind);
    return Sources()[index < kCount ? index : 0];
}

void SetOrnamentSource(OrnamentKind kind, const OrnamentSource& source)
{
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index < kCount)
        Sources()[index] = source;
}

void ResetOrnamentSources()
{
    Sources() = DefaultSources();
}

void DrawOrnament(ImDrawList* dl, OrnamentKind kind, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    const OrnamentSource& source = OrnamentSourceFor(kind);
    if (source.Preferred == GlyphSourceKind::Sprite && source.Sprite.Valid())
    {
        dl->AddImage(source.Sprite.Texture, mn, mx, source.Sprite.Uv0, source.Sprite.Uv1, tint);
        return;
    }
    if (source.Procedural != nullptr)
        source.Procedural(dl, mn, mx, tint);
}
} // namespace EditorChrome
