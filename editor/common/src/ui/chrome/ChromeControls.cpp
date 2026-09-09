#include "ChromeControls.h"

#include "ChromeGeometry.h"
#include "ChromePaint.h"
#include "IconDraw.h"
#include "ui/EditorUiStyle.h"

#include <algorithm>

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
        colors = { held ? Darken(FrameBgActive, 0.15f) : FrameBgActive, Accent, AccentHover, Accent };
        break;
    case ButtonTone::Primary:
        colors = { held ? Darken(Selected, 0.2f) : hovered ? Lighten(Selected, 0.08f) : Selected, SelectedOutline,
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

// The mounted face every button shares: an invisible button for the
// behavior, then the body, sheen, edge, and hover glow painted over its rect.
Face MountedFace(const char* id, ImVec2 size, ButtonTone tone)
{
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##chromebutton", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mx(pos.x + size.x, pos.y + size.y);
    const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
    const float chamfer = std::min(EditorUi::Px(2.0f), size.y * 0.25f);
    const float edge = std::max(1.0f, EditorUi::Px(m.EdgeWidth));
    const ToneColors colors = ColorsFor(tone, hovered, held);

    FillChamfered(dl, ChamferOutline(pos, mx, chamfer), ImGui::GetColorU32(colors.Body));
    // A faint sheen across the top: the inner highlight that makes the body
    // read as a machined face rather than a flat fill.
    if (size.x > chamfer * 2.0f)
        VerticalGradient(dl, ImVec2(pos.x + chamfer, pos.y + edge), ImVec2(mx.x - chamfer, pos.y + size.y * 0.4f),
                         ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, held ? 0.05f : 0.2f)),
                         ImGui::GetColorU32(EditorUi::WithAlpha(EditorUi::MetalHighlight, 0.0f)));

    const float half = edge * 0.5f;
    const ChamferPoly outline = ChamferOutline(ImVec2(pos.x + half, pos.y + half), ImVec2(mx.x - half, mx.y - half), chamfer);
    StrokeChamfered(dl, outline, ImGui::GetColorU32(colors.Edge), edge);
    if (hovered && !held)
        GlowChamfered(dl, outline, colors.Glow, m.GlowAlpha * 0.6f, EditorUi::Px(m.GlowWidth) * 0.7f);

    return Face{ clicked, pos, mx, ImGui::GetColorU32(colors.Label) };
}
}

namespace EditorChrome
{
bool Button(const char* id, const char* label, ImVec2 size, ButtonTone tone)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    if (size.x <= 0.0f)
        size.x = textSize.x + style.FramePadding.x * 2.0f;
    if (size.y <= 0.0f)
        size.y = textSize.y + style.FramePadding.y * 2.0f;

    const Face face = MountedFace(id, size, tone);
    const ImVec2 textPos(std::floor(face.Min.x + (size.x - textSize.x) * 0.5f),
                         std::floor(face.Min.y + (size.y - textSize.y) * 0.5f));
    ImGui::GetWindowDrawList()->AddText(textPos, face.Label, label);
    return face.Clicked;
}

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
