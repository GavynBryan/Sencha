#include "EditorUiStyle.h"

#include "fonts/IconsFontAwesome6.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <string>

namespace
{
#ifndef SENCHA_EDITOR_FONT_DIR
#define SENCHA_EDITOR_FONT_DIR "."
#endif

// Design sizes of the bundled faces, multiplied by UiScale at atlas build.
constexpr float kBodySize = 15.0f;
constexpr float kSmallSize = 12.0f;
constexpr float kLargeSize = 18.0f;
constexpr float kMonoSize = 14.0f;

// Loaded faces; any may stay null (missing TTF), in which case the role falls
// back to ImGui's current default font.
ImFont* g_BodyFont = nullptr;
ImFont* g_SmallFont = nullptr;
ImFont* g_LargeFont = nullptr;
ImFont* g_MonoFont = nullptr;

std::string FontPath(const char* file)
{
    return std::string(SENCHA_EDITOR_FONT_DIR) + "/" + file;
}

bool FontExists(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// Decodes one UTF-8 sequence at [p, end). Malformed input yields U+FFFD and
// consumes the bytes that were there, so a bad string still terminates.
int DecodeUtf8(const char* p, const char* end, unsigned int& out)
{
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x80)
    {
        out = c;
        return 1;
    }
    const int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (len == 1 || p + len > end)
    {
        out = 0xFFFD;
        return static_cast<int>(std::min<std::ptrdiff_t>(len, end - p));
    }
    out = c & (0x7Fu >> len);
    for (int i = 1; i < len; ++i)
        out = (out << 6) | (static_cast<unsigned char>(p[i]) & 0x3Fu);
    return len;
}

// The label roles draw glyph by glyph so tracking and the uppercase transform
// apply without a copy of the string; body-style roles take ImGui's single
// AddText. `emit` receives each glyph's bytes and its x position.
template <typename Emit>
float WalkTrackedText(ImFont& font, const EditorUi::TextStyle& style, std::string_view text,
                      float x, Emit&& emit)
{
    const char* p = text.data();
    const char* end = p + text.size();
    bool first = true;
    while (p < end)
    {
        unsigned int cp = 0;
        const int len = DecodeUtf8(p, end, cp);
        char buf[4];
        int n = len;
        if (style.Uppercase && cp >= 'a' && cp <= 'z')
        {
            cp -= 'a' - 'A';
            buf[0] = static_cast<char>(cp);
            n = 1;
        }
        else
        {
            std::memcpy(buf, p, static_cast<std::size_t>(len));
        }
        if (!first)
            x += style.Tracking;
        first = false;
        emit(buf, buf + n, x);
        const ImFontGlyph* glyph = font.FindGlyph(static_cast<ImWchar>(cp));
        x += glyph != nullptr ? glyph->AdvanceX : 0.0f;
        p += len;
    }
    return x;
}

ImFont& ResolveFont(const EditorUi::TextStyle& style)
{
    return style.Font != nullptr ? *style.Font : *ImGui::GetFont();
}
}

void EditorUi::Apply(ImGuiStyle& style)
{
    // Start from a fresh style so every size field holds its base value before
    // the scale pass at the end: re-applying after a theme switch or a palette
    // edit must not compound UiScale into sizes that were already scaled.
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);

    // We render into an sRGB swapchain (the GPU encodes linear->sRGB on write), so
    // every color ImGui outputs must be LINEAR to land on its authored sRGB value.
    // The EditorUi palette is already linear (see EditorUiStyle.h); linearize the
    // stock-dark seeds here so default-styled widgets match, then overwrite with the
    // palette below (which is already linear, so it must come after this loop).
    for (ImVec4& col : style.Colors)
    {
        col.x = EditorUi::detail::ToLinear(col.x);
        col.y = EditorUi::detail::ToLinear(col.y);
        col.z = EditorUi::detail::ToLinear(col.z);
    }

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                 = TextPrimary;
    c[ImGuiCol_TextDisabled]         = TextDim;
    c[ImGuiCol_WindowBg]             = WindowBg;   // #0A0D0F
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = WindowBg;   // #0A0D0F
    c[ImGuiCol_Border]               = Border;     // #1C3A3A
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = FrameBg;     // #0D1214
    c[ImGuiCol_FrameBgHovered]       = FrameBgHovered;
    c[ImGuiCol_FrameBgActive]        = FrameBgActive;
    c[ImGuiCol_TitleBg]              = HeaderBg;    // #0F1A1C
    c[ImGuiCol_TitleBgActive]        = HeaderBg;
    c[ImGuiCol_TitleBgCollapsed]     = WindowBg;
    c[ImGuiCol_MenuBarBg]            = HeaderBg;
    c[ImGuiCol_ScrollbarBg]          = WindowBg;
    c[ImGuiCol_ScrollbarGrab]        = Secondary;       // purple
    c[ImGuiCol_ScrollbarGrabHovered] = SecondaryHover;
    c[ImGuiCol_ScrollbarGrabActive]  = SecondaryHover;
    c[ImGuiCol_CheckMark]            = Accent;          // teal
    c[ImGuiCol_SliderGrab]           = Accent;          // teal
    c[ImGuiCol_SliderGrabActive]     = AccentHover;
    c[ImGuiCol_Button]               = ButtonBg;        // #112022
    c[ImGuiCol_ButtonHovered]        = ButtonHovered;   // #1A3535
    c[ImGuiCol_ButtonActive]         = Accent;          // teal
    c[ImGuiCol_Header]               = Selected;        // #003D35 selection highlight
    c[ImGuiCol_HeaderHovered]        = ButtonHovered;
    c[ImGuiCol_HeaderActive]         = Selected;
    c[ImGuiCol_Separator]            = Secondary;       // purple separators
    c[ImGuiCol_SeparatorHovered]     = SecondaryHover;
    c[ImGuiCol_SeparatorActive]      = SecondaryHover;
    c[ImGuiCol_Tab]                  = HeaderBg;
    c[ImGuiCol_TabHovered]           = ButtonHovered;
    c[ImGuiCol_TabActive]            = Selected;        // #003D35 (teal-dark active)
    c[ImGuiCol_TabUnfocused]         = HeaderBg;
    c[ImGuiCol_TabUnfocusedActive]   = FrameBg;
    c[ImGuiCol_ResizeGrip]           = Secondary;       // purple secondary
    c[ImGuiCol_ResizeGripHovered]    = SecondaryHover;
    c[ImGuiCol_ResizeGripActive]     = Accent;
    c[ImGuiCol_TextSelectedBg]       = WithAlpha(Accent, 0.35f);
    c[ImGuiCol_DragDropTarget]       = Accent;
    c[ImGuiCol_NavHighlight]         = Accent;

    // Sharp corners everywhere — beveled metal panels, not rounded cards.
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.FrameRounding     = 0.0f;
    style.GrabRounding      = 0.0f;
    style.TabRounding       = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.PopupRounding     = 0.0f;
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.TabBorderSize     = 0.0f;
    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(7.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 5.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.IndentSpacing     = 18.0f;
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 10.0f;

    // Every ImGui size above is a design pixel; this is the one place they meet
    // the display. Fonts were built at the same factor by LoadFonts.
    style.ScaleAllSizes(UiScale);
}

ImFont* EditorUi::MonoFont()
{
    return g_MonoFont;
}

void EditorUi::LoadFonts(ImGuiIO& io)
{
    const std::string ui = FontPath("JetBrainsMono-Regular.ttf");
    const std::string icons = FontPath("fa-solid-900.ttf");

    const float bodySize = Px(kBodySize);

    // UI font (default): monospace for the terminal/console look. If absent, keep
    // ImGui's built-in so the editor still runs.
    if (FontExists(ui))
        g_BodyFont = io.Fonts->AddFontFromFileTTF(ui.c_str(), bodySize);
    else
        io.Fonts->AddFontDefault();

    // Merge Font Awesome icon glyphs into the default font so ICON_FA_* literals
    // render inline in labels. Range static: the atlas builder reads it lazily,
    // so it must outlive this call.
    if (FontExists(icons))
    {
        static const ImWchar kIconRange[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.PixelSnapH = true;
        cfg.GlyphMinAdvanceX = bodySize; // monospaced icon column
        io.Fonts->AddFontFromFileTTF(icons.c_str(), bodySize - Px(1.0f), &cfg, kIconRange);
    }

    // The label faces (uppercase tracked titles, status readouts) and the
    // console / numeric readout face. Same family, other sizes.
    if (FontExists(ui))
    {
        g_SmallFont = io.Fonts->AddFontFromFileTTF(ui.c_str(), Px(kSmallSize));
        g_LargeFont = io.Fonts->AddFontFromFileTTF(ui.c_str(), Px(kLargeSize));
        g_MonoFont = io.Fonts->AddFontFromFileTTF(ui.c_str(), Px(kMonoSize));
    }
}

EditorUi::TextStyle EditorUi::StyleFor(TextRole role)
{
    const float tracking = Px(Metrics.Tracking);
    switch (role)
    {
    case TextRole::ApplicationTitle: return { g_LargeFont, TextPrimary, true, tracking * 1.5f };
    case TextRole::PanelTitle:       return { g_SmallFont, Accent, true, tracking };
    case TextRole::SectionTitle:     return { g_SmallFont, TextPrimary, true, tracking };
    case TextRole::Body:             return { g_BodyFont, TextPrimary, false, 0.0f };
    case TextRole::Data:             return { g_MonoFont, TextPrimary, false, 0.0f };
    case TextRole::SecondaryText:      return { g_SmallFont, TextDim, false, 0.0f };
    case TextRole::Status:           return { g_SmallFont, TextDim, true, tracking };
    }
    return { g_BodyFont, TextPrimary, false, 0.0f };
}

ImVec2 EditorUi::MeasureRoleText(TextRole role, std::string_view text)
{
    const TextStyle style = StyleFor(role);
    ImFont& font = ResolveFont(style);
    if (!style.Uppercase && style.Tracking <= 0.0f)
        return font.CalcTextSizeA(font.FontSize, FLT_MAX, 0.0f, text.data(), text.data() + text.size());
    const float width = WalkTrackedText(font, style, text, 0.0f, [](const char*, const char*, float) {});
    return ImVec2(width, font.FontSize);
}

void EditorUi::DrawRoleText(ImDrawList* dl, ImVec2 pos, TextRole role, std::string_view text, ImU32 colorOverride)
{
    const TextStyle style = StyleFor(role);
    ImFont& font = ResolveFont(style);
    const ImU32 color = colorOverride != 0 ? colorOverride : ImGui::GetColorU32(style.Color);
    if (!style.Uppercase && style.Tracking <= 0.0f)
    {
        dl->AddText(&font, font.FontSize, pos, color, text.data(), text.data() + text.size());
        return;
    }
    WalkTrackedText(font, style, text, pos.x, [&](const char* begin, const char* end, float x) {
        dl->AddText(&font, font.FontSize, ImVec2(x, pos.y), color, begin, end);
    });
}
