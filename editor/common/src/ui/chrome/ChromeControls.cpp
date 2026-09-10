#include "ChromeControls.h"

#include "ChromeGeometry.h"
#include "ChromePaint.h"
#include "IconDraw.h"
#include "ui/EditorUiStyle.h"

#include <algorithm>
#include <cstring>

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

    return Face{ clicked, pos, mx, ImGui::GetColorU32(colors.Label) };
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
