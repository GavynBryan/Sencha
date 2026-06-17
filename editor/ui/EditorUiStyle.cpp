#include "EditorUiStyle.h"

#include "fonts/IconsFontAwesome6.h"

#include <filesystem>
#include <string>

namespace
{
ImVec4 WithAlpha(ImVec4 c, float a) { return ImVec4(c.x, c.y, c.z, a); }

#ifndef SENCHA_EDITOR_FONT_DIR
#define SENCHA_EDITOR_FONT_DIR "."
#endif

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
}

void EditorUi::Apply(ImGuiStyle& style)
{
    // Seed from the stock dark theme so any entry we don't override stays sane,
    // then paint the palette over the entries that define the look.
    ImGui::StyleColorsDark(&style);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                 = TextPrimary;
    c[ImGuiCol_TextDisabled]         = TextDim;
    c[ImGuiCol_WindowBg]             = PanelBg;
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = HeaderBg;
    c[ImGuiCol_Border]               = Border;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = FrameBg;
    c[ImGuiCol_FrameBgHovered]       = FrameBgHovered;
    c[ImGuiCol_FrameBgActive]        = FrameBgActive;
    c[ImGuiCol_TitleBg]              = HeaderBg;
    c[ImGuiCol_TitleBgActive]        = HeaderBg;
    c[ImGuiCol_TitleBgCollapsed]     = WindowBg;
    c[ImGuiCol_MenuBarBg]            = HeaderBg;
    c[ImGuiCol_ScrollbarBg]          = WindowBg;
    c[ImGuiCol_ScrollbarGrab]        = Border;
    c[ImGuiCol_ScrollbarGrabHovered] = AccentDim;
    c[ImGuiCol_ScrollbarGrabActive]  = Accent;
    c[ImGuiCol_CheckMark]            = Accent;
    c[ImGuiCol_SliderGrab]           = AccentDim;
    c[ImGuiCol_SliderGrabActive]     = Accent;
    c[ImGuiCol_Button]               = FrameBg;
    c[ImGuiCol_ButtonHovered]        = FrameBgHovered;
    c[ImGuiCol_ButtonActive]         = AccentDim;
    c[ImGuiCol_Header]               = Selected;
    c[ImGuiCol_HeaderHovered]        = AccentDim;
    c[ImGuiCol_HeaderActive]         = Accent;
    c[ImGuiCol_Separator]            = Border;
    c[ImGuiCol_SeparatorHovered]     = AccentDim;
    c[ImGuiCol_SeparatorActive]      = Accent;
    c[ImGuiCol_Tab]                  = HeaderBg;
    c[ImGuiCol_TabHovered]           = AccentDim;
    c[ImGuiCol_TabActive]            = FrameBgActive;
    c[ImGuiCol_TabUnfocused]         = HeaderBg;
    c[ImGuiCol_TabUnfocusedActive]   = FrameBg;
    c[ImGuiCol_ResizeGrip]           = Border;
    c[ImGuiCol_ResizeGripHovered]    = AccentDim;
    c[ImGuiCol_ResizeGripActive]     = Accent;
    c[ImGuiCol_TextSelectedBg]       = WithAlpha(Accent, 0.35f);
    c[ImGuiCol_DragDropTarget]       = Accent;
    c[ImGuiCol_NavHighlight]         = Accent;

    style.WindowRounding    = 2.0f;
    style.ChildRounding     = 2.0f;
    style.FrameRounding     = 2.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.PopupRounding     = 2.0f;
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
}

ImFont* EditorUi::MonoFont()
{
    return g_MonoFont;
}

void EditorUi::LoadFonts(ImGuiIO& io)
{
    constexpr float kUiSize = 15.0f;
    constexpr float kMonoSize = 14.0f;

    const std::string ui = FontPath("Inter-Regular.ttf");
    const std::string icons = FontPath("fa-solid-900.ttf");
    const std::string mono = FontPath("JetBrainsMono-Regular.ttf");

    // UI font (default). If absent, keep ImGui's built-in so the editor still runs.
    if (FontExists(ui))
        io.Fonts->AddFontFromFileTTF(ui.c_str(), kUiSize);
    else
        io.Fonts->AddFontDefault();

    // Merge Font Awesome icon glyphs into the default font so ICON_FA_* literals
    // render inline in labels (gates the Phase 2 icon toolbar). Range static: the
    // atlas builder reads it lazily, so it must outlive this call.
    if (FontExists(icons))
    {
        static const ImWchar kIconRange[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.PixelSnapH = true;
        cfg.GlyphMinAdvanceX = kUiSize; // monospaced icon column
        io.Fonts->AddFontFromFileTTF(icons.c_str(), kUiSize - 1.0f, &cfg, kIconRange);
    }

    // Monospace font for the console / numeric readouts.
    if (FontExists(mono))
        g_MonoFont = io.Fonts->AddFontFromFileTTF(mono.c_str(), kMonoSize);
}
