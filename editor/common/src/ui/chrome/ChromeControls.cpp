#include "ChromeControls.h"

#include "ChromeGeometry.h"
#include "ChromePaint.h"
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
    switch (tone)
    {
    case ButtonTone::Active:
        return { held ? Darken(FrameBgActive, 0.15f) : FrameBgActive, hovered ? AccentHover : Accent, AccentHover, Accent };
    case ButtonTone::Primary:
        return { held ? Darken(Selected, 0.2f) : hovered ? Lighten(Selected, 0.08f) : Selected, SelectedOutline,
                 SelectedOutline, SelectedOutline };
    case ButtonTone::Destructive:
        return { held ? Darken(Danger, 0.8f) : hovered ? Darken(Danger, 0.65f) : Darken(Danger, 0.75f), Danger,
                 Lighten(Danger, 0.35f), Danger };
    case ButtonTone::Normal:
        break;
    }
    return { held ? FrameBgActive : hovered ? FrameBgHovered : FrameBg, hovered ? Accent : Border,
             hovered ? AccentHover : Accent, Accent };
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

    const ImVec2 textPos(std::floor(pos.x + (size.x - textSize.x) * 0.5f), std::floor(pos.y + (size.y - textSize.y) * 0.5f));
    dl->AddText(textPos, ImGui::GetColorU32(colors.Label), label);
    return clicked;
}
} // namespace EditorChrome
