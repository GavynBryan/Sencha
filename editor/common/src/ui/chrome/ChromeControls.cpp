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
}

void EditorChrome::DrawToolWheel(ImDrawList* dl, const WheelPaint& wheel)
{
    if (wheel.Slots.empty())
        return;

    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    const float line = std::max(1.0f, EditorUi::Px(m.EdgeWidth));
    const ImU32 border = ImGui::GetColorU32(EditorUi::Border);
    // The wedges reach as far as the slots do; beyond that the target is the
    // direction alone, which paint cannot show.
    const float outer = wheel.Radius + wheel.Slots.front().Size * 0.5f + EditorUi::Px(6.0f);

    // Spokes first, under everything, so the sectors read before any is hot.
    for (const WheelSlot& slot : wheel.Slots)
    {
        const float a = ArcAngle(slot.Angle0);
        dl->AddLine(ImVec2(wheel.Center.x + std::cos(a) * wheel.Hub, wheel.Center.y + std::sin(a) * wheel.Hub),
                    ImVec2(wheel.Center.x + std::cos(a) * outer, wheel.Center.y + std::sin(a) * outer),
                    ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::Border, 0.6f)), line);
    }

    for (const WheelSlot& slot : wheel.Slots)
    {
        if (!slot.Hot)
            continue;
        // The whole wedge, hub to outer arc, as one concave polygon.
        const float a0 = ArcAngle(slot.Angle0);
        const float a1 = ArcAngle(slot.Angle1);
        dl->PathArcTo(wheel.Center, outer, a0, a1);
        dl->PathArcTo(wheel.Center, wheel.Hub, a1, a0);
        std::vector<ImVec2> wedge(dl->_Path.begin(), dl->_Path.end());
        dl->PathClear();
        dl->AddConcavePolyFilled(wedge.data(), static_cast<int>(wedge.size()),
                                 ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::FrameBgHovered, m.GlowAlpha * 2.0f)));
        dl->PathArcTo(wheel.Center, outer, a0, a1);
        dl->PathStroke(ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::ControlHover, m.GlowAlpha)), 0,
                       EditorUi::Px(m.GlowWidth));
        dl->PathArcTo(wheel.Center, outer, a0, a1);
        dl->PathStroke(ImGui::GetColorU32(EditorUi::ControlHover), 0, line);
    }

    dl->AddCircle(wheel.Center, outer, border, 0, line);
    dl->AddCircleFilled(wheel.Center, wheel.Hub, ImGui::GetColorU32(EditorUi::ChassisBg));
    dl->AddCircle(wheel.Center, wheel.Hub, border, 0, line);

    for (const WheelSlot& slot : wheel.Slots)
    {
        const ImVec2 mn(slot.Center.x - slot.Size * 0.5f, slot.Center.y - slot.Size * 0.5f);
        const ImVec2 mx(slot.Center.x + slot.Size * 0.5f, slot.Center.y + slot.Size * 0.5f);
        const ButtonTone tone = slot.Active ? ButtonTone::Active : ButtonTone::Normal;
        if (slot.Icon != IconId::None)
            DrawIconButton(dl, mn, mx, slot.Icon, tone, true, slot.Hot);
        else
            DrawTextButton(dl, mn, mx, slot.Label, tone, true, slot.Hot);
    }

    if (!wheel.Caption.empty())
    {
        const ImVec2 size = EditorUi::MeasureRoleText(EditorUi::TextRole::PanelTitle, wheel.Caption);
        const ImVec2 pos(std::floor(wheel.Center.x - size.x * 0.5f), std::floor(wheel.CaptionY));
        EditorUi::DrawRoleText(dl, pos, EditorUi::TextRole::PanelTitle, wheel.Caption,
                               wheel.CaptionDim ? ImGui::GetColorU32(EditorUi::TextDim) : 0);
    }
}
