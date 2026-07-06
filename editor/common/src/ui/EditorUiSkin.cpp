#include "EditorUiSkin.h"

#include "EditorSkin.h"
#include "EditorUiStyle.h"

#include <imgui_internal.h> // GetCurrentWindow / TitleBarRect for the title glow

#include <algorithm>
#include <cstring>

namespace
{
// Look tunables — dial the whole skin from here.
constexpr float kGloss     = 0.12f; // gradient top-lighten / bottom-darken
constexpr float kBevel     = 0.30f; // bevel highlight/shadow strength
constexpr float kBandTop   = 0.14f; // band top highlight
constexpr float kBandBot   = 0.12f; // band bottom darken
constexpr float kPanelTop  = 0.06f; // panel backdrop top lighten
constexpr float kPanelBot  = 0.05f; // panel backdrop bottom darken

// Active texture skin (set at init); null => gradient fallback.
const EditorSkin* g_Skin = nullptr;
}

void EditorUiSkin::SetActiveSkin(const EditorSkin* skin)
{
    g_Skin = skin;
}

void EditorUiSkin::Draw9Slice(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx,
                              const SkinElement& s, ImU32 tint)
{
    const float w = s.Size.x, h = s.Size.y;
    const float u1 = s.Inset / w, u2 = 1.0f - s.Inset / w;
    const float v1 = s.Inset / h, v2 = 1.0f - s.Inset / h;
    // Clamp the dest border so it never exceeds half the rect (small widgets).
    const float di = std::min(s.Inset, std::min((mx.x - mn.x) * 0.5f, (mx.y - mn.y) * 0.5f));
    const float x0 = mn.x, x1 = mn.x + di, x2 = mx.x - di, x3 = mx.x;
    const float y0 = mn.y, y1 = mn.y + di, y2 = mx.y - di, y3 = mx.y;
    const ImTextureID t = s.Texture;
    const auto cell = [&](float ax, float ay, float bx, float by,
                          float au, float av, float bu, float bv) {
        dl->AddImage(t, ImVec2(ax, ay), ImVec2(bx, by), ImVec2(au, av), ImVec2(bu, bv), tint);
    };
    cell(x0, y0, x1, y1, 0,  0,  u1, v1); cell(x1, y0, x2, y1, u1, 0,  u2, v1); cell(x2, y0, x3, y1, u2, 0,  1,  v1);
    cell(x0, y1, x1, y2, 0,  v1, u1, v2); cell(x1, y1, x2, y2, u1, v1, u2, v2); cell(x2, y1, x3, y2, u2, v1, 1,  v2);
    cell(x0, y2, x1, y3, 0,  v2, u1, 1 ); cell(x1, y2, x2, y3, u1, v2, u2, 1 ); cell(x2, y2, x3, y3, u2, v2, 1,  1 );
}

void EditorUiSkin::GlowText(const ImVec2& pos, ImU32 textColor, const ImVec4& glowColor, const char* text)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 halo = ImGui::GetColorU32(WithAlpha(glowColor, 0.30f));
    const float o = 1.0f;
    const ImVec2 offs[8] = {
        { -o, 0 }, { o, 0 }, { 0, -o }, { 0, o },
        { -o, -o }, { o, -o }, { -o, o }, { o, o },
    };
    for (const ImVec2& d : offs)
        dl->AddText(ImVec2(pos.x + d.x, pos.y + d.y), halo, text);
    dl->AddText(pos, textColor, text);
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
    if (g_Skin != nullptr && g_Skin->Band.Valid())
    {
        Draw9Slice(dl, mn, mx, g_Skin->Band, IM_COL32_WHITE);
        return;
    }

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

    if (g_Skin != nullptr && g_Skin->Button.Valid())
    {
        // Neutral metal face, tinted cyan when active; pressed darkens slightly.
        const ImU32 tint = active ? ImGui::GetColorU32(EditorUi::AccentHover)
                         : held   ? ImGui::GetColorU32(WithAlpha(EditorUi::TextPrimary, 0.85f))
                                  : IM_COL32_WHITE;
        Draw9Slice(dl, pos, mx, g_Skin->Button, tint);
    }
    else
    {
        const ImVec4 base = active ? EditorUi::AccentDim
                          : hovered ? EditorUi::FrameBgHovered
                                    : EditorUi::FrameBg;
        GradientBevel(dl, pos, mx, base, /*raised=*/!held);
    }

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

// Glowing L-shaped accent brackets at the four corners of [mn,mx] — the mockup's
// signature "analog" panel detail. A dim wide leg behind a crisp bright leg gives a
// soft neon glow. Drawn into `dl` (the window draw list, so it stays panel-local).
static void CornerBrackets(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx)
{
    const float L = 13.0f;  // leg length
    const float i = 1.5f;   // inset from the very edge (sit inside the steel border)
    const ImU32 glow  = ImGui::GetColorU32(EditorUiSkin::WithAlpha(EditorUi::Accent, 0.30f));
    const ImU32 crisp = ImGui::GetColorU32(EditorUi::Accent);
    const auto bracket = [&](float cx, float cy, float sx, float sy) {
        // Wide soft glow leg, then the crisp 1px leg on top.
        dl->AddLine(ImVec2(cx, cy), ImVec2(cx + sx * L, cy), glow, 3.0f);
        dl->AddLine(ImVec2(cx, cy), ImVec2(cx, cy + sy * L), glow, 3.0f);
        dl->AddLine(ImVec2(cx, cy), ImVec2(cx + sx * L, cy), crisp, 1.0f);
        dl->AddLine(ImVec2(cx, cy), ImVec2(cx, cy + sy * L), crisp, 1.0f);
    };
    bracket(mn.x + i, mn.y + i,  1.0f,  1.0f); // top-left
    bracket(mx.x - i, mn.y + i, -1.0f,  1.0f); // top-right
    bracket(mn.x + i, mx.y - i,  1.0f, -1.0f); // bottom-left
    bracket(mx.x - i, mx.y - i, -1.0f, -1.0f); // bottom-right
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

    if (g_Skin != nullptr && g_Skin->Frame.Valid())
        // Beveled metal frame + dark interior over the panel body.
        Draw9Slice(dl, mn, mx, g_Skin->Frame, IM_COL32_WHITE);
    else
    {
        const ImU32 top = ImGui::GetColorU32(Lighten(EditorUi::PanelBg, kPanelTop));
        const ImU32 bot = ImGui::GetColorU32(Darken(EditorUi::PanelBg, kPanelBot));
        dl->AddRectFilledMultiColor(mn, mx, top, top, bot, bot);
        dl->AddLine(ImVec2(mn.x, mn.y + 0.5f), ImVec2(mx.x, mn.y + 0.5f),
                    ImGui::GetColorU32(WithAlpha(Lighten(EditorUi::PanelBg, 0.18f), 0.7f)));
    }

    // Accent brackets framing the whole panel (title bar included) — the analog look.
    CornerBrackets(dl, wpos, mx);

    // Glowing teal panel title: ImGui already drew the title text (plain) during
    // Begin(); overlay a haloed accent copy on top so panel titles read as the
    // glowing label (the per-widget glow, e.g. the inspector entity header, is plain).
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w != nullptr && (w->Flags & ImGuiWindowFlags_NoTitleBar) == 0)
    {
        // Visible label = window name up to the "##" id separator.
        char label[128];
        const char* name = w->Name ? w->Name : "";
        const char* hashes = std::strstr(name, "##");
        const std::size_t len = hashes ? static_cast<std::size_t>(hashes - name) : std::strlen(name);
        const std::size_t n = std::min(len, sizeof(label) - 1);
        std::memcpy(label, name, n);
        label[n] = '\0';

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImRect tb = w->TitleBarRect();
        float x = tb.Min.x + style.FramePadding.x;
        if ((w->Flags & ImGuiWindowFlags_NoCollapse) == 0)
            x += ImGui::GetFontSize() + style.FramePadding.x; // skip the collapse arrow
        const float y = tb.Min.y + style.FramePadding.y;
        GlowText(ImVec2(x, y), ImGui::GetColorU32(EditorUi::AccentHover), EditorUi::Accent, label);
    }
}
