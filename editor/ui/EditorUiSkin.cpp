#include "EditorUiSkin.h"

#include "EditorUiStyle.h"

namespace
{
// Look tunables — dial the whole skin from here.
constexpr float kGloss     = 0.12f; // gradient top-lighten / bottom-darken
constexpr float kBevel     = 0.30f; // bevel highlight/shadow strength
constexpr float kBandTop   = 0.14f; // band top highlight
constexpr float kBandBot   = 0.12f; // band bottom darken
constexpr float kPanelTop  = 0.06f; // panel backdrop top lighten
constexpr float kPanelBot  = 0.05f; // panel backdrop bottom darken
}

ImVec4 EditorUiSkin::Lighten(const ImVec4& c, float a)
{
    return ImVec4(c.x + (1.0f - c.x) * a, c.y + (1.0f - c.y) * a, c.z + (1.0f - c.z) * a, c.w);
}

ImVec4 EditorUiSkin::Darken(const ImVec4& c, float a)
{
    return ImVec4(c.x * (1.0f - a), c.y * (1.0f - a), c.z * (1.0f - a), c.w);
}

ImVec4 EditorUiSkin::WithAlpha(const ImVec4& c, float a)
{
    return ImVec4(c.x, c.y, c.z, a);
}

void EditorUiSkin::GradientBevel(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx,
                                 const ImVec4& base, bool raised)
{
    const ImU32 top = ImGui::GetColorU32(raised ? Lighten(base, kGloss) : Darken(base, kBandBot));
    const ImU32 bot = ImGui::GetColorU32(raised ? Darken(base, kGloss)  : Lighten(base, kGloss * 0.5f));
    dl->AddRectFilledMultiColor(mn, mx, top, top, bot, bot);

    const ImU32 light = ImGui::GetColorU32(WithAlpha(Lighten(base, kBevel), 0.9f));
    const ImU32 dark  = ImGui::GetColorU32(WithAlpha(Darken(base, kBevel), 0.9f));
    const ImU32 tl = raised ? light : dark; // top + left
    const ImU32 br = raised ? dark : light;  // bottom + right
    dl->AddLine(ImVec2(mn.x, mn.y + 0.5f), ImVec2(mx.x, mn.y + 0.5f), tl);
    dl->AddLine(ImVec2(mn.x + 0.5f, mn.y), ImVec2(mn.x + 0.5f, mx.y), tl);
    dl->AddLine(ImVec2(mn.x, mx.y - 0.5f), ImVec2(mx.x, mx.y - 0.5f), br);
    dl->AddLine(ImVec2(mx.x - 0.5f, mn.y), ImVec2(mx.x - 0.5f, mx.y), br);
}

void EditorUiSkin::Band(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const ImVec4& base)
{
    const ImU32 top = ImGui::GetColorU32(Lighten(base, kBandTop));
    const ImU32 bot = ImGui::GetColorU32(Darken(base, kBandBot));
    dl->AddRectFilledMultiColor(mn, mx, top, top, bot, bot);
    dl->AddLine(ImVec2(mn.x, mn.y + 0.5f), ImVec2(mx.x, mn.y + 0.5f),
                ImGui::GetColorU32(WithAlpha(Lighten(base, 0.28f), 0.8f)));
    // Thin cyan lip along the bottom — the signature skinned edge.
    dl->AddLine(ImVec2(mn.x, mx.y - 0.5f), ImVec2(mx.x, mx.y - 0.5f),
                ImGui::GetColorU32(WithAlpha(EditorUi::Accent, 0.35f)));
}

bool EditorUiSkin::Button(const char* id, const char* label, const ImVec2& size, bool active)
{
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##skinbtn", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mx(pos.x + size.x, pos.y + size.y);

    const ImVec4 base = active ? EditorUi::AccentDim
                      : hovered ? EditorUi::FrameBgHovered
                                : EditorUi::FrameBg;
    GradientBevel(dl, pos, mx, base, /*raised=*/!held);

    if (hovered || active)
    {
        const ImVec4 glow = active ? EditorUi::AccentHover : EditorUi::Accent;
        dl->AddRect(pos, mx, ImGui::GetColorU32(WithAlpha(glow, hovered ? 0.95f : 0.6f)));
    }

    const ImVec2 ts = ImGui::CalcTextSize(label);
    const ImVec2 tp(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f);
    const ImU32 txt = ImGui::GetColorU32(active ? EditorUi::AccentHover : EditorUi::TextPrimary);
    dl->AddText(tp, txt, label);

    return clicked;
}

void EditorUiSkin::PanelBackdrop()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Cursor is at the content top-left (below the title bar) right after Begin();
    // span down to the window's bottom-right so the title bar is left untouched.
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 wpos = ImGui::GetWindowPos();
    const ImVec2 wsize = ImGui::GetWindowSize();
    const ImVec2 mx(wpos.x + wsize.x, wpos.y + wsize.y);
    if (mx.x <= mn.x || mx.y <= mn.y)
        return;

    const ImU32 top = ImGui::GetColorU32(Lighten(EditorUi::PanelBg, kPanelTop));
    const ImU32 bot = ImGui::GetColorU32(Darken(EditorUi::PanelBg, kPanelBot));
    dl->AddRectFilledMultiColor(mn, mx, top, top, bot, bot);
    dl->AddLine(ImVec2(mn.x, mn.y + 0.5f), ImVec2(mx.x, mn.y + 0.5f),
                ImGui::GetColorU32(WithAlpha(Lighten(EditorUi::PanelBg, 0.18f), 0.7f)));
}
