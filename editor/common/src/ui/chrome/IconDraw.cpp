#include "IconDraw.h"
#include "ui/EditorUiStyle.h"

#include "fonts/IconsFontAwesome6.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

namespace
{
using namespace EditorChrome;

constexpr float kPi = 3.14159265f;

// A unit square mapped onto the icon's rect: every drawing is authored in
// 0..1 coordinates so one set of strokes serves every size the chrome asks
// for. Stroke width follows the size.
struct Canvas
{
    ImDrawList* Dl;
    ImVec2 Mn;
    float S;
    ImU32 C;
    float W;

    Canvas(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint)
        : Dl(dl)
        , C(tint)
    {
        S = std::max(1.0f, std::min(mx.x - mn.x, mx.y - mn.y));
        Mn = ImVec2(std::floor(mn.x + (mx.x - mn.x - S) * 0.5f), std::floor(mn.y + (mx.y - mn.y - S) * 0.5f));
        W = std::max(EditorUi::Px(1.25f), S * 0.1f);
    }

    [[nodiscard]] ImVec2 P(float u, float v) const { return ImVec2(Mn.x + u * S, Mn.y + v * S); }
    void Line(float u0, float v0, float u1, float v1, float k = 1.0f) const
    {
        Dl->AddLine(P(u0, v0), P(u1, v1), C, W * k);
    }
    void Rect(float u0, float v0, float u1, float v1, float k = 1.0f) const
    {
        Dl->AddRect(P(u0, v0), P(u1, v1), C, 0.0f, 0, W * k);
    }
    void RectFilled(float u0, float v0, float u1, float v1) const { Dl->AddRectFilled(P(u0, v0), P(u1, v1), C); }
    void Circle(float u, float v, float r, float k = 1.0f) const { Dl->AddCircle(P(u, v), r * S, C, 0, W * k); }
    void Dot(float u, float v, float r) const { Dl->AddCircleFilled(P(u, v), r * S, C); }
    void Outline(std::initializer_list<ImVec2> uv, float k = 1.0f) const
    {
        for (const ImVec2& p : uv)
            Dl->PathLineTo(P(p.x, p.y));
        Dl->PathStroke(C, ImDrawFlags_Closed, W * k);
    }
    void Fill(std::initializer_list<ImVec2> uv) const
    {
        for (const ImVec2& p : uv)
            Dl->PathLineTo(P(p.x, p.y));
        Dl->PathFillConcave(C);
    }
    // Arc around (u, v), angles in radians with 0 at the right and the
    // positive direction clockwise on screen.
    void Arc(float u, float v, float r, float a0, float a1, float k = 1.0f) const
    {
        Dl->PathArcTo(P(u, v), r * S, a0, a1);
        Dl->PathStroke(C, 0, W * k);
    }
    // A filled arrowhead at (u, v) pointing along (du, dv).
    void Head(float u, float v, float du, float dv, float len = 0.16f) const
    {
        const float n = std::sqrt(du * du + dv * dv);
        if (n <= 0.0f)
            return;
        du /= n;
        dv /= n;
        const float bx = u - du * len;
        const float by = v - dv * len;
        const float hw = len * 0.55f;
        Dl->AddTriangleFilled(P(u, v), P(bx - dv * hw, by + du * hw), P(bx + dv * hw, by - du * hw), C);
    }
};

void Pointer(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Fill({ { 0.28f, 0.14f }, { 0.28f, 0.78f }, { 0.43f, 0.63f }, { 0.55f, 0.88f }, { 0.66f, 0.82f }, { 0.54f, 0.58f }, { 0.75f, 0.56f } });
}

void Box(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Outline({ { 0.5f, 0.12f }, { 0.85f, 0.31f }, { 0.85f, 0.69f }, { 0.5f, 0.88f }, { 0.15f, 0.69f }, { 0.15f, 0.31f } });
    k.Line(0.5f, 0.5f, 0.5f, 0.88f);
    k.Line(0.5f, 0.5f, 0.15f, 0.31f);
    k.Line(0.5f, 0.5f, 0.85f, 0.31f);
}

void Plane(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Outline({ { 0.12f, 0.72f }, { 0.38f, 0.32f }, { 0.88f, 0.32f }, { 0.62f, 0.72f } });
}

void Cylinder(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    dl->AddEllipse(k.P(0.5f, 0.28f), ImVec2(0.32f * k.S, 0.12f * k.S), c, 0.0f, 0, k.W);
    k.Line(0.18f, 0.28f, 0.18f, 0.72f);
    k.Line(0.82f, 0.28f, 0.82f, 0.72f);
    dl->PathEllipticalArcTo(k.P(0.5f, 0.72f), ImVec2(0.32f * k.S, 0.12f * k.S), 0.0f, 0.0f, kPi);
    dl->PathStroke(c, 0, k.W);
}

void Move(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Line(0.5f, 0.22f, 0.5f, 0.78f);
    k.Line(0.22f, 0.5f, 0.78f, 0.5f);
    k.Head(0.5f, 0.1f, 0.0f, -1.0f);
    k.Head(0.5f, 0.9f, 0.0f, 1.0f);
    k.Head(0.1f, 0.5f, -1.0f, 0.0f);
    k.Head(0.9f, 0.5f, 1.0f, 0.0f);
}

void Rotate(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    const float a0 = kPi * 0.1f;
    const float a1 = kPi * 1.7f;
    k.Arc(0.5f, 0.5f, 0.32f, a0, a1);
    const float ex = 0.5f + 0.32f * std::cos(a1);
    const float ey = 0.5f + 0.32f * std::sin(a1);
    k.Head(ex - std::sin(a1) * 0.02f, ey + std::cos(a1) * 0.02f, -std::sin(a1), std::cos(a1), 0.18f);
}

void ChevronDown(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Line(0.2f, 0.35f, 0.5f, 0.65f);
    k.Line(0.5f, 0.65f, 0.8f, 0.35f);
}

void Scale(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Rect(0.15f, 0.55f, 0.45f, 0.85f);
    k.Line(0.45f, 0.55f, 0.82f, 0.18f);
    k.Line(0.58f, 0.15f, 0.85f, 0.15f);
    k.Line(0.85f, 0.15f, 0.85f, 0.42f);
}

void Resize(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Line(0.25f, 0.75f, 0.75f, 0.25f);
    k.Line(0.5f, 0.22f, 0.78f, 0.22f);
    k.Line(0.78f, 0.22f, 0.78f, 0.5f);
    k.Line(0.22f, 0.5f, 0.22f, 0.78f);
    k.Line(0.22f, 0.78f, 0.5f, 0.78f);
}

void Pivot(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Circle(0.5f, 0.5f, 0.28f);
    k.Line(0.5f, 0.1f, 0.5f, 0.3f);
    k.Line(0.5f, 0.7f, 0.5f, 0.9f);
    k.Line(0.1f, 0.5f, 0.3f, 0.5f);
    k.Line(0.7f, 0.5f, 0.9f, 0.5f);
    k.Dot(0.5f, 0.5f, 0.06f);
}

void Anchor(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Circle(0.5f, 0.2f, 0.09f);
    k.Line(0.5f, 0.29f, 0.5f, 0.86f);
    k.Line(0.32f, 0.42f, 0.68f, 0.42f);
    k.Arc(0.5f, 0.55f, 0.32f, kPi * 0.15f, kPi * 0.85f);
}

void Light(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Rect(0.4f, 0.4f, 0.6f, 0.6f);
    k.Line(0.5f, 0.08f, 0.5f, 0.28f);
    k.Line(0.5f, 0.72f, 0.5f, 0.92f);
    k.Line(0.08f, 0.5f, 0.28f, 0.5f);
    k.Line(0.72f, 0.5f, 0.92f, 0.5f);
    k.Line(0.18f, 0.18f, 0.32f, 0.32f);
    k.Line(0.68f, 0.68f, 0.82f, 0.82f);
    k.Line(0.18f, 0.82f, 0.32f, 0.68f);
    k.Line(0.68f, 0.32f, 0.82f, 0.18f);
}

void Grid(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Rect(0.15f, 0.15f, 0.85f, 0.85f);
    k.Line(0.383f, 0.15f, 0.383f, 0.85f, 0.8f);
    k.Line(0.617f, 0.15f, 0.617f, 0.85f, 0.8f);
    k.Line(0.15f, 0.383f, 0.85f, 0.383f, 0.8f);
    k.Line(0.15f, 0.617f, 0.85f, 0.617f, 0.8f);
}

void GridFrame(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Line(0.383f, 0.15f, 0.383f, 0.85f, 0.7f);
    k.Line(0.617f, 0.15f, 0.617f, 0.85f, 0.7f);
    k.Line(0.15f, 0.383f, 0.85f, 0.383f, 0.7f);
    k.Line(0.15f, 0.617f, 0.85f, 0.617f, 0.7f);
    k.Line(0.15f, 0.12f, 0.15f, 0.88f, 2.0f);
    k.Line(0.12f, 0.85f, 0.88f, 0.85f, 2.0f);
}

void Snap(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Line(0.15f, 0.35f, 0.15f, 0.15f); k.Line(0.15f, 0.15f, 0.35f, 0.15f);
    k.Line(0.65f, 0.15f, 0.85f, 0.15f); k.Line(0.85f, 0.15f, 0.85f, 0.35f);
    k.Line(0.85f, 0.65f, 0.85f, 0.85f); k.Line(0.85f, 0.85f, 0.65f, 0.85f);
    k.Line(0.35f, 0.85f, 0.15f, 0.85f); k.Line(0.15f, 0.85f, 0.15f, 0.65f);
    k.Line(0.3f, 0.5f, 0.7f, 0.5f);
    k.Line(0.5f, 0.3f, 0.5f, 0.7f);
}

void ZoneBounds(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Line(0.15f, 0.38f, 0.15f, 0.15f); k.Line(0.15f, 0.15f, 0.38f, 0.15f);
    k.Line(0.62f, 0.15f, 0.85f, 0.15f); k.Line(0.85f, 0.15f, 0.85f, 0.38f);
    k.Line(0.85f, 0.62f, 0.85f, 0.85f); k.Line(0.85f, 0.85f, 0.62f, 0.85f);
    k.Line(0.38f, 0.85f, 0.15f, 0.85f); k.Line(0.15f, 0.85f, 0.15f, 0.62f);
    k.Dot(0.5f, 0.5f, 0.06f);
}

void Play(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Fill({ { 0.28f, 0.15f }, { 0.82f, 0.5f }, { 0.28f, 0.85f } });
}

void Stop(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.RectFilled(0.24f, 0.24f, 0.76f, 0.76f);
}

void Hammer(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Line(0.22f, 0.86f, 0.58f, 0.5f, 2.0f);
    k.Fill({ { 0.48f, 0.4f }, { 0.78f, 0.1f }, { 0.9f, 0.22f }, { 0.6f, 0.52f } });
}

void Cancel(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Line(0.22f, 0.22f, 0.78f, 0.78f, 1.5f);
    k.Line(0.78f, 0.22f, 0.22f, 0.78f, 1.5f);
}

void Check(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    dl->PathLineTo(k.P(0.18f, 0.55f));
    dl->PathLineTo(k.P(0.42f, 0.8f));
    dl->PathLineTo(k.P(0.84f, 0.26f));
    dl->PathStroke(c, 0, k.W * 1.6f);
}

void Folder(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Outline({ { 0.12f, 0.25f }, { 0.4f, 0.25f }, { 0.48f, 0.35f }, { 0.88f, 0.35f }, { 0.88f, 0.8f }, { 0.12f, 0.8f } });
}

void Search(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Circle(0.42f, 0.42f, 0.24f);
    k.Line(0.6f, 0.6f, 0.86f, 0.86f, 1.8f);
}

void Add(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Line(0.5f, 0.18f, 0.5f, 0.82f, 1.5f);
    k.Line(0.18f, 0.5f, 0.82f, 0.5f, 1.5f);
}

void Delete(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Line(0.18f, 0.28f, 0.82f, 0.28f, 1.3f);
    k.Line(0.42f, 0.18f, 0.58f, 0.18f);
    dl->PathLineTo(k.P(0.27f, 0.3f));
    dl->PathLineTo(k.P(0.32f, 0.86f));
    dl->PathLineTo(k.P(0.68f, 0.86f));
    dl->PathLineTo(k.P(0.73f, 0.3f));
    dl->PathStroke(c, 0, k.W);
    k.Line(0.43f, 0.42f, 0.45f, 0.74f, 0.8f);
    k.Line(0.57f, 0.42f, 0.55f, 0.74f, 0.8f);
}

void EyeShape(const Canvas& k)
{
    k.Dl->PathLineTo(k.P(0.1f, 0.5f));
    k.Dl->PathBezierQuadraticCurveTo(k.P(0.5f, 0.08f), k.P(0.9f, 0.5f));
    k.Dl->PathBezierQuadraticCurveTo(k.P(0.5f, 0.92f), k.P(0.1f, 0.5f));
    k.Dl->PathStroke(k.C, 0, k.W);
    k.Dot(0.5f, 0.5f, 0.12f);
}

void Eye(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    EyeShape(Canvas(dl, mn, mx, c));
}

void EyeOff(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    EyeShape(k);
    k.Line(0.2f, 0.86f, 0.8f, 0.14f, 1.3f);
}

void Lock(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Rect(0.25f, 0.45f, 0.75f, 0.85f);
    k.Arc(0.5f, 0.4f, 0.18f, kPi, kPi * 2.0f);
    k.Line(0.32f, 0.4f, 0.32f, 0.45f);
    k.Line(0.68f, 0.4f, 0.68f, 0.45f);
    k.Dot(0.5f, 0.64f, 0.06f);
}

void Unlock(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Rect(0.25f, 0.45f, 0.75f, 0.85f);
    k.Arc(0.5f, 0.34f, 0.18f, kPi, kPi * 2.0f);
    k.Line(0.68f, 0.34f, 0.68f, 0.45f);
    k.Line(0.32f, 0.34f, 0.32f, 0.28f);
    k.Dot(0.5f, 0.64f, 0.06f);
}

void Cut(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Line(0.35f, 0.36f, 0.86f, 0.72f, 1.4f);
    k.Line(0.35f, 0.64f, 0.86f, 0.28f, 1.4f);
    k.Circle(0.24f, 0.28f, 0.12f);
    k.Circle(0.24f, 0.72f, 0.12f);
}

void Carve(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Outline({ { 0.15f, 0.15f }, { 0.85f, 0.15f }, { 0.85f, 0.5f }, { 0.55f, 0.5f }, { 0.55f, 0.85f }, { 0.15f, 0.85f } });
    k.Line(0.62f, 0.62f, 0.85f, 0.85f, 0.8f);
}

void ModeVertex(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Outline({ { 0.5f, 0.15f }, { 0.85f, 0.8f }, { 0.15f, 0.8f } }, 0.6f);
    k.Dot(0.5f, 0.15f, 0.1f);
    k.Dot(0.85f, 0.8f, 0.1f);
    k.Dot(0.15f, 0.8f, 0.1f);
}

void ModeEdge(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Outline({ { 0.5f, 0.15f }, { 0.85f, 0.8f }, { 0.15f, 0.8f } }, 0.6f);
    k.Line(0.15f, 0.8f, 0.85f, 0.8f, 2.2f);
    k.Dot(0.85f, 0.8f, 0.09f);
    k.Dot(0.15f, 0.8f, 0.09f);
}

void ModeFace(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    const ImU32 fill = (c & 0x00FFFFFFu) | (static_cast<ImU32>(((c >> 24) & 0xFFu) * 0.45f) << 24);
    dl->AddTriangleFilled(k.P(0.5f, 0.15f), k.P(0.85f, 0.8f), k.P(0.15f, 0.8f), fill);
    k.Outline({ { 0.5f, 0.15f }, { 0.85f, 0.8f }, { 0.15f, 0.8f } });
}

void WindowMinimize(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Line(0.22f, 0.72f, 0.78f, 0.72f, 1.6f);
}

void WindowMaximize(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Rect(0.2f, 0.2f, 0.8f, 0.8f, 1.2f);
}

void WindowRestore(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 c)
{
    const Canvas k(dl, mn, mx, c);
    k.Rect(0.18f, 0.36f, 0.66f, 0.84f, 1.1f);
    k.Line(0.36f, 0.36f, 0.36f, 0.18f); k.Line(0.36f, 0.18f, 0.84f, 0.18f);
    k.Line(0.84f, 0.18f, 0.84f, 0.66f); k.Line(0.84f, 0.66f, 0.66f, 0.66f);
}

constexpr std::size_t kCount = static_cast<std::size_t>(IconId::Count);

std::array<IconSource, kCount> DefaultSources()
{
    std::array<IconSource, kCount> t{};
    const auto row = [&](IconId id, ProceduralGlyphFn fn, const char* glyph) {
        t[static_cast<std::size_t>(id)] = IconSource{ GlyphSourceKind::Procedural, fn, glyph, {} };
    };
    row(IconId::Pointer, Pointer, ICON_FA_ARROW_POINTER);
    row(IconId::Move, Move, ICON_FA_UP_DOWN_LEFT_RIGHT);
    row(IconId::Rotate, Rotate, ICON_FA_ROTATE);
    row(IconId::Scale, Scale, ICON_FA_MAXIMIZE);
    row(IconId::Resize, Resize, ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER);
    row(IconId::Pivot, Pivot, ICON_FA_CROSSHAIRS);
    row(IconId::Anchor, Anchor, ICON_FA_ANCHOR);
    row(IconId::Box, Box, ICON_FA_CUBE);
    row(IconId::Plane, Plane, ICON_FA_SQUARE);
    row(IconId::Cylinder, Cylinder, ICON_FA_DATABASE);
    row(IconId::Light, Light, ICON_FA_LIGHTBULB);
    row(IconId::Grid, Grid, ICON_FA_BORDER_ALL);
    row(IconId::GridFrame, GridFrame, ICON_FA_RULER_COMBINED);
    row(IconId::Snap, Snap, ICON_FA_MAGNET);
    row(IconId::ZoneBounds, ZoneBounds, ICON_FA_VECTOR_SQUARE);
    row(IconId::Play, Play, ICON_FA_PLAY);
    row(IconId::Stop, Stop, ICON_FA_STOP);
    row(IconId::Hammer, Hammer, ICON_FA_HAMMER);
    row(IconId::Cancel, Cancel, ICON_FA_XMARK);
    row(IconId::Check, Check, ICON_FA_CHECK);
    row(IconId::Folder, Folder, ICON_FA_FOLDER);
    row(IconId::Search, Search, ICON_FA_MAGNIFYING_GLASS);
    row(IconId::Refresh, Rotate, ICON_FA_ARROWS_ROTATE);
    row(IconId::ChevronDown, ChevronDown, ICON_FA_CHEVRON_DOWN);
    row(IconId::Add, Add, ICON_FA_PLUS);
    row(IconId::Delete, Delete, ICON_FA_TRASH);
    row(IconId::Eye, Eye, ICON_FA_EYE);
    row(IconId::EyeOff, EyeOff, ICON_FA_EYE_SLASH);
    row(IconId::Lock, Lock, ICON_FA_LOCK);
    row(IconId::Unlock, Unlock, ICON_FA_LOCK_OPEN);
    row(IconId::Cut, Cut, ICON_FA_SCISSORS);
    row(IconId::Carve, Carve, ICON_FA_CROP_SIMPLE);
    row(IconId::ModeObject, Box, ICON_FA_CUBE);
    row(IconId::ModeVertex, ModeVertex, ICON_FA_CIRCLE_DOT);
    row(IconId::ModeEdge, ModeEdge, ICON_FA_GRIP_LINES);
    row(IconId::ModeFace, ModeFace, ICON_FA_VECTOR_SQUARE);
    row(IconId::WindowMinimize, WindowMinimize, ICON_FA_WINDOW_MINIMIZE);
    row(IconId::WindowMaximize, WindowMaximize, ICON_FA_WINDOW_MAXIMIZE);
    row(IconId::WindowRestore, WindowRestore, ICON_FA_WINDOW_RESTORE);
    row(IconId::WindowClose, Cancel, ICON_FA_XMARK);
    return t;
}

std::array<IconSource, kCount>& Sources()
{
    static std::array<IconSource, kCount> sources = DefaultSources();
    return sources;
}

bool DrawFrom(const IconSource& source, GlyphSourceKind kind, ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    switch (kind)
    {
    case GlyphSourceKind::Sprite:
        if (!source.Sprite.Valid())
            return false;
        dl->AddImage(source.Sprite.Texture, mn, mx, source.Sprite.Uv0, source.Sprite.Uv1, tint);
        return true;
    case GlyphSourceKind::Procedural:
        if (source.Procedural == nullptr)
            return false;
        source.Procedural(dl, mn, mx, tint);
        return true;
    case GlyphSourceKind::FontGlyph:
    {
        if (source.FontGlyph == nullptr)
            return false;
        const ImVec2 size = ImGui::CalcTextSize(source.FontGlyph);
        dl->AddText(ImVec2(std::floor(mn.x + (mx.x - mn.x - size.x) * 0.5f), std::floor(mn.y + (mx.y - mn.y - size.y) * 0.5f)),
                    tint, source.FontGlyph);
        return true;
    }
    }
    return false;
}
}

namespace EditorChrome
{
const IconSource& IconSourceFor(IconId id)
{
    const std::size_t index = static_cast<std::size_t>(id);
    return Sources()[index < kCount ? index : 0];
}

void SetIconSource(IconId id, const IconSource& source)
{
    const std::size_t index = static_cast<std::size_t>(id);
    if (index < kCount)
        Sources()[index] = source;
}

void ResetIconSources()
{
    Sources() = DefaultSources();
}

void DrawIcon(ImDrawList* dl, IconId id, ImVec2 mn, ImVec2 mx, ImU32 tint)
{
    const IconSource& source = IconSourceFor(id);
    if (DrawFrom(source, source.Preferred, dl, mn, mx, tint))
        return;
    for (GlyphSourceKind kind : { GlyphSourceKind::Sprite, GlyphSourceKind::Procedural, GlyphSourceKind::FontGlyph })
        if (kind != source.Preferred && DrawFrom(source, kind, dl, mn, mx, tint))
            return;
}
} // namespace EditorChrome
