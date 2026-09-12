#include "ChromeControls.h"

#include "ChromeGeometry.h"
#include "ChromePaint.h"
#include "IconDraw.h"
#include "ui/EditorUiStyle.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

namespace
{
using EditorChrome::ButtonTone;

struct ToneColors
{
    ImVec4 Body;
    ImVec4 Edge;
    ImVec4 Label;
    ImVec4 Glow;
};

ToneColors ColorsFor(ButtonTone tone, bool hovered, bool held)
{
    using namespace EditorUi;
    ToneColors colors;
    switch (tone)
    {
    case ButtonTone::Active:
        colors = { Darken(Selected, held ? 0.25f : 0.15f), SelectedOutline,
                   SelectedOutline, SelectedOutline };
        break;
    case ButtonTone::Destructive:
        colors = { held ? Darken(Danger, 0.8f) : hovered ? Darken(Danger, 0.65f) : Darken(Danger, 0.75f), Danger,
                   Lighten(Danger, 0.35f), Danger };
        break;
    case ButtonTone::Normal:
        colors = { held ? FrameBgActive : hovered ? FrameBgHovered : FrameBg, Border, Accent, Accent };
        break;
    }
    // Hover is one color for every tone: the edge and its glow turn yellow
    // while the label keeps the tone's voice.
    if (hovered && !held)
    {
        colors.Edge = ControlHover;
        colors.Glow = ControlHover;
    }
    return colors;
}
}

namespace
{
using namespace EditorChrome;

struct Face
{
    bool Clicked = false;
    ImVec2 Min{};
    ImVec2 Max{};
    ImU32 Label = 0;
};

// The face every button shares, painted over `pos`..`mx`: body, sheen, edge,
// and hover glow, in the tone's colours. Returns the label tint. Mounted and
// floating buttons both paint through here, so a control over the viewport
// reads the same as one in a panel.
ImU32 PaintFace(ImDrawList* dl, ImVec2 pos, ImVec2 mx, ButtonTone tone, bool hovered, bool held)
{
    const ImVec2 size(mx.x - pos.x, mx.y - pos.y);
    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    const float chamfer = std::clamp(std::min(size.x, size.y) * 0.16f, EditorUi::Px(2.0f), EditorUi::Px(4.0f));
    const float edge = std::max(1.0f, EditorUi::Px(m.EdgeWidth));
    const ToneColors colors = ColorsFor(tone, hovered, held);

    const int vertexStart = dl->VtxBuffer.Size;
    FillChamfered(dl, ChamferOutline(pos, mx, chamfer), ImGui::GetColorU32(colors.Body));
    // Tint the polygon's vertices so the gradient follows the chamfered silhouette.
    const ImVec4 top = EditorUi::Lighten(colors.Body, held ? 0.03f : 0.12f);
    const ImVec4 bottom = EditorUi::Darken(colors.Body, 0.25f);
    for (int i = vertexStart; i < dl->VtxBuffer.Size; ++i)
    {
        const float t = std::clamp((dl->VtxBuffer[i].pos.y - pos.y) / size.y, 0.0f, 1.0f);
        ImVec4 color(top.x + (bottom.x - top.x) * t, top.y + (bottom.y - top.y) * t,
                     top.z + (bottom.z - top.z) * t, colors.Body.w);
        // Preserve the anti-aliased fringe's transparent vertices.
        const ImU32 alpha = dl->VtxBuffer[i].col & IM_COL32_A_MASK;
        dl->VtxBuffer[i].col = (ImGui::GetColorU32(color) & ~IM_COL32_A_MASK) | alpha;
    }
    if (size.x > chamfer * 2.0f)
        dl->AddLine(ImVec2(pos.x + chamfer, pos.y + edge), ImVec2(mx.x - chamfer, pos.y + edge),
                    ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, 0.25f)), edge);

    const float half = edge * 0.5f;
    const ChamferPoly outline = ChamferOutline(ImVec2(pos.x + half, pos.y + half), ImVec2(mx.x - half, mx.y - half), chamfer);
    StrokeChamfered(dl, outline, ImGui::GetColorU32(colors.Edge), edge);
    if (hovered && !held)
        GlowChamfered(dl, outline, colors.Glow, m.GlowAlpha * 0.6f, EditorUi::Px(m.GlowWidth) * 0.7f);

    return ImGui::GetColorU32(colors.Label);
}

// The mounted face: an invisible button for the behavior, then the face
// painted over its rect.
Face MountedFace(const char* id, ImVec2 size, ButtonTone tone)
{
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##chromebutton", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImGui::PopID();

    const ImVec2 mx(pos.x + size.x, pos.y + size.y);
    return Face{ clicked, pos, mx, PaintFace(ImGui::GetWindowDrawList(), pos, mx, tone, hovered, held) };
}
}

namespace EditorChrome
{
bool Button(const char* id, const char* label, ImVec2 size, ButtonTone tone)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 textSize = ImGui::CalcTextSize(label, nullptr, true);
    if (size.x <= 0.0f)
        size.x = textSize.x + style.FramePadding.x * 2.0f;
    if (size.y <= 0.0f)
        size.y = textSize.y + style.FramePadding.y * 2.0f;

    const Face face = MountedFace(id, size, tone);
    const ImVec2 textPos(std::floor(face.Min.x + (size.x - textSize.x) * 0.5f),
                         std::floor(face.Min.y + (size.y - textSize.y) * 0.5f));
    ImGui::GetWindowDrawList()->AddText(textPos, face.Label, label, std::strstr(label, "##"));
    return face.Clicked;
}

bool BeginCombo(const char* label, const char* preview)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const float width = ImGui::CalcItemWidth();
    const float height = ImGui::GetFrameHeight();
    const ImVec2 mx(mn.x + width, mn.y + height);
    const bool open = ImGui::BeginCombo(label, preview, ImGuiComboFlags_NoArrowButton);
    // An open combo has already entered its popup. Only the captured parent
    // draw list and geometry may be used to paint the housing from here.
    const bool hovered = !open && ImGui::IsItemHovered();
    const ImVec2 housing(std::max(mn.x, mx.x - height), mn.y);
    dl->AddRectFilled(housing, mx, ImGui::GetColorU32(EditorUi::FrameBg));
    dl->AddLine(housing, ImVec2(housing.x, mx.y), ImGui::GetColorU32(EditorUi::Border),
                EditorUi::Px(EditorUi::Metrics.EdgeWidth));
    const float inset = height * 0.3f;
    const ImVec4 tint = open ? EditorUi::SelectedOutline : hovered ? EditorUi::ControlHover : EditorUi::Accent;
    DrawIcon(dl, IconId::ChevronDown, ImVec2(housing.x + inset, mn.y + inset),
             ImVec2(mx.x - inset, mx.y - inset), ImGui::GetColorU32(tint));
    return open;
}

void EndCombo() { ImGui::EndCombo(); }

bool IconButton(const char* id, IconId icon, float size, ButtonTone tone)
{
    const Face face = MountedFace(id, ImVec2(size, size), tone);
    // The glyph sits inside the face with a margin, so its strokes never
    // touch the edge.
    const float inset = std::floor(size * 0.2f);
    DrawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(face.Min.x + inset, face.Min.y + inset),
             ImVec2(face.Max.x - inset, face.Max.y - inset), face.Label);
    return face.Clicked;
}

bool ToolButton(const char* id, IconId icon, const char* tooltip, bool active, float size)
{
    const bool clicked = IconButton(id, icon, size, active ? ButtonTone::Active : ButtonTone::Normal);
    if (tooltip != nullptr && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

bool ToolButton(const char* id, const char* label, const char* tooltip, bool active, float size)
{
    const bool clicked = Button(id, label, ImVec2(size, size), active ? ButtonTone::Active : ButtonTone::Normal);
    if (tooltip != nullptr && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    return clicked;
}
} // namespace EditorChrome

namespace
{
// The floating face both painted buttons share; only the glyph differs. A
// disabled one keeps its tone's body but goes quiet: no hover, dim label.
ImU32 DrawFloatingFace(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ButtonTone tone, bool enabled, bool hot)
{
    const ImU32 label = PaintFace(dl, mn, mx, tone, hot && enabled, false);
    return enabled ? label : ImGui::GetColorU32(EditorUi::TextDim);
}
}

void EditorChrome::DrawIconButton(ImDrawList* dl, ImVec2 mn, ImVec2 mx, IconId icon, ButtonTone tone,
                                  bool enabled, bool hot)
{
    const ImU32 tint = DrawFloatingFace(dl, mn, mx, tone, enabled, hot);
    const float inset = EditorUi::Px(4.0f);
    DrawIcon(dl, icon, ImVec2(mn.x + inset, mn.y + inset), ImVec2(mx.x - inset, mx.y - inset), tint);
}

void EditorChrome::DrawTextButton(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const char* label,
                                  ButtonTone tone, bool enabled, bool hot)
{
    const ImU32 tint = DrawFloatingFace(dl, mn, mx, tone, enabled, hot);
    if (label == nullptr || label[0] == '\0')
        return;
    const ImVec2 size = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2((mn.x + mx.x - size.x) * 0.5f, (mn.y + mx.y - size.y) * 0.5f), tint, label);
}

void EditorChrome::DrawDial(ImDrawList* dl, std::span<const ImVec2> rim, std::span<const ImVec2> ticks,
                            ImVec2 knob, bool hot)
{
    if (rim.size() < 2)
        return;

    const ImU32 rimColor = ImGui::GetColorU32(hot ? EditorUi::ControlHover : EditorUi::Border);
    dl->AddPolyline(rim.data(), static_cast<int>(rim.size()), rimColor, ImDrawFlags_Closed,
                    EditorUi::Px(1.0f));

    const ImU32 tickColor = ImGui::GetColorU32(EditorUi::TextDim);
    const float tickRadius = EditorUi::Px(1.5f);
    for (const ImVec2& tick : ticks)
        dl->AddCircleFilled(tick, tickRadius, tickColor);

    // The knob reads as the thing to grab: a filled dot on the accent, ringed so
    // it stays legible over whatever the viewport is showing behind it.
    const float knobRadius = EditorUi::Px(4.0f);
    dl->AddCircleFilled(knob, knobRadius,
                        ImGui::GetColorU32(hot ? EditorUi::ControlHover : EditorUi::Accent));
    dl->AddCircle(knob, knobRadius, ImGui::GetColorU32(EditorUi::ChassisBg), 0, EditorUi::Px(1.0f));
}

namespace
{
// Screen angle for the wheel's convention (clockwise from straight up) as an
// ImGui arc angle (counter-clockwise from +x in screen space).
float ArcAngle(float wheelAngle)
{
    return wheelAngle - std::numbers::pi_v<float> * 0.5f;
}

ImVec2 OnCircle(ImVec2 center, float radius, float arcAngle)
{
    return { center.x + std::cos(arcAngle) * radius, center.y + std::sin(arcAngle) * radius };
}

// A closed outline lit segment by segment from the chrome's one light, so a
// curve bevels the way a chamfered plate does: the side facing the light
// takes the highlight, the side away from it the shadow. `points` runs
// clockwise on screen.
void BevelOutline(ImDrawList* dl, std::span<const ImVec2> points, ImU32 highlight, ImU32 shadow, float width)
{
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const ImVec2 a = points[i];
        const ImVec2 b = points[(i + 1) % points.size()];
        dl->AddLine(a, b, EditorChrome::EdgeLit(a, b) ? highlight : shadow, width);
    }
}

// A circle as a clockwise outline, for BevelOutline.
std::vector<ImVec2> CirclePoints(ImVec2 center, float radius, int segments)
{
    std::vector<ImVec2> points;
    points.reserve(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i)
        points.push_back(OnCircle(center, radius, 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(segments)));
    return points;
}

// The annular wedge between two radii and two angles, as a clockwise
// outline: outer arc forwards, inner arc back.
std::vector<ImVec2> WedgePoints(ImVec2 center, float inner, float outer, float a0, float a1)
{
    const int segments = std::max(4, static_cast<int>(std::ceil((a1 - a0) * outer / 6.0f)));
    std::vector<ImVec2> points;
    points.reserve(static_cast<std::size_t>(segments + 1) * 2);
    for (int i = 0; i <= segments; ++i)
        points.push_back(OnCircle(center, outer, a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(segments)));
    for (int i = segments; i >= 0; --i)
        points.push_back(OnCircle(center, inner, a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(segments)));
    return points;
}

void StrokeArc(ImDrawList* dl, ImVec2 center, float radius, float a0, float a1, ImU32 color, float width)
{
    dl->PathArcTo(center, radius, a0, a1);
    dl->PathStroke(color, 0, width);
}

// The colours and the line weight one wheel is painted with, so both rings
// are the same metal under the same light.
struct WheelPalette
{
    ImU32 Highlight = 0;
    ImU32 Shadow = 0;
    ImU32 Chassis = 0;
    ImU32 Accent = 0;
    ImU32 AccentDim = 0;
    ImU32 Orange = 0;
    float Line = 1.0f;
};

// The seam is a fixed width, so its angle narrows with radius: taken at the
// mid radius of the petal so the gap reads even along the flank.
float SeamAngle(float seam, float inner, float outer)
{
    return seam / ((inner + outer) * 0.5f);
}

// One petal of a ring: the wedge between `inner` and `outer` cut into the
// plate, its glyph in a recess. Orange on the hot petal; the active slot's
// glyph is orange on a plain petal.
void DrawPetal(ImDrawList* dl, ImVec2 center, float inner, float outer, float seamAngle,
               const EditorChrome::WheelSlot& slot, const WheelPalette& p)
{
    const float a0 = ArcAngle(slot.Angle0) + seamAngle;
    const float a1 = ArcAngle(slot.Angle1) - seamAngle;
    if (a1 <= a0)
        return;
    const std::vector<ImVec2> petal = WedgePoints(center, inner, outer, a0, a1);
    const ImVec4 face = slot.Hot ? EditorUi::Selected : EditorUi::FrameBg;
    dl->AddConcavePolyFilled(petal.data(), static_cast<int>(petal.size()), ImGui::GetColorU32(face));
    if (slot.Hot)
        dl->AddPolyline(petal.data(), static_cast<int>(petal.size()), p.Orange, ImDrawFlags_Closed, p.Line);
    else
        BevelOutline(dl, petal, p.Highlight, p.Shadow, p.Line);

    // The glyph sits in a shallow recess, lit by the same light.
    const float well = slot.Size * 0.42f;
    const std::vector<ImVec2> ring = CirclePoints(slot.Center, well, 24);
    dl->AddCircleFilled(slot.Center, well, slot.Hot ? ImGui::GetColorU32(EditorUi::Darken(EditorUi::Selected, 0.35f)) : p.Chassis);
    BevelOutline(dl, ring, slot.Hot ? p.Orange : p.Shadow, slot.Hot ? p.Orange : p.Highlight, p.Line);
    const float glyph = slot.Size * 0.6f;
    const ImU32 tint = slot.Hot ? ImGui::GetColorU32(EditorUi::Lighten(EditorUi::SelectedOutline, 0.35f))
                     : slot.Active ? p.Orange
                                   : p.Accent;
    if (slot.Icon != IconId::None)
        EditorChrome::DrawIcon(dl, slot.Icon, ImVec2(slot.Center.x - glyph * 0.5f, slot.Center.y - glyph * 0.5f),
                               ImVec2(slot.Center.x + glyph * 0.5f, slot.Center.y + glyph * 0.5f), tint);
    else if (slot.Label != nullptr && slot.Label[0] != '\0')
    {
        const char glyphText[2] = { slot.Label[0], '\0' };
        const ImVec2 size = ImGui::CalcTextSize(glyphText);
        dl->AddText(ImVec2(slot.Center.x - size.x * 0.5f, slot.Center.y - size.y * 0.5f), tint, glyphText);
    }
}

// The rim closing a ring at `outer`: a band of metal, beveled on both
// circles, a hairline gap to the petals, a tick at each seam, and the active
// slot's orange mark over its wedge.
void DrawRim(ImDrawList* dl, ImVec2 center, float outer, float rim, float seamAngle,
             std::span<const EditorChrome::WheelSlot> slots, const WheelPalette& p)
{
    const float rimInner = outer - rim;
    dl->PathArcTo(center, outer, 0.0f, 2.0f * std::numbers::pi_v<float>);
    dl->PathArcTo(center, rimInner, 2.0f * std::numbers::pi_v<float>, 0.0f);
    std::vector<ImVec2> band(dl->_Path.begin(), dl->_Path.end());
    dl->PathClear();
    dl->AddConcavePolyFilled(band.data(), static_cast<int>(band.size()), ImGui::GetColorU32(EditorUi::MetalBase));
    BevelOutline(dl, CirclePoints(center, outer - p.Line * 0.5f, 64), p.Highlight, p.Shadow, p.Line);
    // The inner circle is lit as the edge of a hole: its outward normal points
    // in, so the walk runs the other way.
    std::vector<ImVec2> innerRing = CirclePoints(center, rimInner + p.Line * 0.5f, 64);
    std::reverse(innerRing.begin(), innerRing.end());
    BevelOutline(dl, innerRing, p.Highlight, p.Shadow, p.Line);
    dl->AddCircle(center, rimInner - p.Line * 0.5f, p.Chassis, 0, p.Line);

    const float detailRadius = outer - rim * 0.5f;
    for (const EditorChrome::WheelSlot& slot : slots)
    {
        const float a = ArcAngle(slot.Angle0);
        dl->AddLine(OnCircle(center, rimInner + rim * 0.25f, a), OnCircle(center, outer - rim * 0.25f, a), p.AccentDim, p.Line);
        if (slot.Active)
            StrokeArc(dl, center, detailRadius, ArcAngle(slot.Angle0) + seamAngle * 2.0f,
                      ArcAngle(slot.Angle1) - seamAngle * 2.0f, p.Orange, p.Line * 2.0f);
    }
}
}

// The wheel is one machined part: a dark plate with the petals cut into it,
// a chunky rim around them, a recessed hub at the axle, and a ledge under
// the rim carrying the caption. A hot tool's variants are a second ring of
// the same petals and rim cut into the same plate outside the first. Depth
// comes from the shared bevel light on every edge, straight or curved;
// colour is the metals, with cyan kept thin and at the perimeter, and orange
// on exactly the petal the pointer is in.
void EditorChrome::DrawToolWheel(ImDrawList* dl, const WheelPaint& wheel)
{
    if (wheel.Slots.empty())
        return;

    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    const WheelPalette p{
        .Highlight = ImGui::GetColorU32(EditorUi::MetalHighlight),
        .Shadow = ImGui::GetColorU32(EditorUi::MetalShadow),
        .Chassis = ImGui::GetColorU32(EditorUi::ChassisBg),
        .Accent = ImGui::GetColorU32(EditorUi::Accent),
        .AccentDim = ImGui::GetColorU32(EditorUi::AccentDim),
        .Orange = ImGui::GetColorU32(EditorUi::SelectedOutline),
        .Line = std::max(1.0f, EditorUi::Px(m.EdgeWidth)),
    };
    const float seam = wheel.Seam;
    const float rim = wheel.Rim;
    const float slotSize = wheel.Slots.front().Size;
    const float outer = wheel.Radius + slotSize * 0.5f + seam + rim;
    const float petalOuter = outer - rim - seam;
    const float petalInner = wheel.Hub + seam;
    const bool fan = !wheel.Variants.empty() && wheel.OuterRadius > 0.0f;
    const float fanInner = outer + seam;
    const float fanOuter = wheel.OuterRadius + slotSize * 0.5f;
    const float extent = fan ? fanOuter + seam + rim : outer;

    // Plate: the dark body everything is cut into, as far as the wheel reaches.
    dl->AddCircleFilled(wheel.Center, extent, p.Shadow);

    const float seamAngle = SeamAngle(seam, petalInner, petalOuter);
    for (const WheelSlot& slot : wheel.Slots)
        DrawPetal(dl, wheel.Center, petalInner, petalOuter, seamAngle, slot, p);
    DrawRim(dl, wheel.Center, outer, rim, seamAngle, wheel.Slots, p);

    if (fan)
    {
        const float fanSeam = SeamAngle(seam, fanInner, fanOuter);
        for (const WheelSlot& slot : wheel.Variants)
            DrawPetal(dl, wheel.Center, fanInner, fanOuter, fanSeam, slot, p);
        DrawRim(dl, wheel.Center, extent, rim, fanSeam, wheel.Variants, p);
    }

    // Perimeter detail, sparse: a thin cyan arc over the top-left quadrant
    // and a shorter one bottom-right, on the primary rim.
    const float detailRadius = outer - rim * 0.5f;
    StrokeArc(dl, wheel.Center, detailRadius, std::numbers::pi_v<float> * 1.05f, std::numbers::pi_v<float> * 1.45f, p.Accent, p.Line);
    StrokeArc(dl, wheel.Center, detailRadius, std::numbers::pi_v<float> * 0.15f, std::numbers::pi_v<float> * 0.35f, p.Accent, p.Line);

    // Hub: a small recess at the axle, its edge lit as a hole's is.
    dl->AddCircleFilled(wheel.Center, wheel.Hub, p.Chassis);
    std::vector<ImVec2> hubRing = CirclePoints(wheel.Center, wheel.Hub - p.Line * 0.5f, 32);
    std::reverse(hubRing.begin(), hubRing.end());
    BevelOutline(dl, hubRing, p.Highlight, p.Shadow, p.Line);
    dl->AddCircleFilled(wheel.Center, std::max(p.Line, EditorUi::Px(2.0f)), p.AccentDim);

    // Caption: a shallow ledge under the outermost rim, part of the chassis
    // rather than a plate hung off it.
    if (!wheel.Caption.empty())
    {
        const ImVec2 textSize = EditorUi::MeasureRoleText(EditorUi::TextRole::Status, wheel.Caption);
        const float padX = EditorUi::Px(10.0f);
        const float ledgeHeight = textSize.y + EditorUi::Px(6.0f);
        const float halfWidth = std::max(textSize.x * 0.5f + padX, rim * 2.0f);
        const float top = wheel.Center.y + extent - rim * 0.5f;
        const ImVec2 mn(wheel.Center.x - halfWidth, top);
        const ImVec2 mx(wheel.Center.x + halfWidth, top + ledgeHeight);
        const ChamferPoly ledge = ChamferOutline(mn, mx, std::min(EditorUi::Px(4.0f), ledgeHeight * 0.5f));
        FillChamfered(dl, ledge, ImGui::GetColorU32(EditorUi::MetalBase));
        const float half = p.Line * 0.5f;
        BevelChamfered(dl, ChamferOutline(ImVec2(mn.x + half, mn.y + half), ImVec2(mx.x - half, mx.y - half),
                                          std::min(EditorUi::Px(4.0f), ledgeHeight * 0.5f)),
                       p.Highlight, p.Shadow, p.Line);
        const ImU32 textColor = wheel.CaptionDim ? ImGui::GetColorU32(EditorUi::TextDim) : p.Orange;
        EditorUi::DrawRoleText(dl, ImVec2(std::floor(wheel.Center.x - textSize.x * 0.5f), std::floor(mn.y + (ledgeHeight - textSize.y) * 0.5f)),
                               EditorUi::TextRole::Status, wheel.Caption, textColor);
    }
}
