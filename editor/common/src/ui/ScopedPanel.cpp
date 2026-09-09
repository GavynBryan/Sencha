#include "ScopedPanel.h"

#include "EditorUiStyle.h"
#include "chrome/ChromeFrame.h"
#include "chrome/ChromeGeometry.h"
#include "chrome/ChromeHeader.h"

namespace
{
struct WindowRect
{
    ImVec2 Min;
    ImVec2 Max;
};

WindowRect CurrentWindowRect()
{
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    return { pos, ImVec2(pos.x + size.x, pos.y + size.y) };
}
}

ScopedPanel::ScopedPanel(std::string_view title, bool* open, ImGuiWindowFlags flags)
    : Open(ImGui::Begin(title.data(), open, flags))
{
    if (Open)
        EditorUiSkin::PanelBackdrop();
}

ScopedPanel::ScopedPanel(std::string_view title, bool* open, PanelStyle style, ImGuiWindowFlags flags)
    : Chrome(true)
    , Style(style)
{
    // The padding keeps widgets inside the ring; ImGui reads it at Begin.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, EditorChrome::ContentPadding(style));
    Open = ImGui::Begin(title.data(), open, flags);
    ImGui::PopStyleVar();
    if (!Open)
        return;

    const WindowRect rect = CurrentWindowRect();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // ImGui clips the body to the window's inner rect; the chrome spans the
    // whole window, so it draws under its own clip.
    dl->PushClipRect(rect.Min, rect.Max, false);
    EditorChrome::DrawFrameBase(dl, rect.Min, rect.Max, style);
    dl->PopClipRect();

    // Content starts under the rail; it scrolls beneath it later.
    const EditorChrome::FrameSpec spec = EditorChrome::SpecFor(style);
    if (spec.Rail > 0.0f)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + spec.Rail + EditorUi::Px(2.0f));
}

ScopedPanel::~ScopedPanel()
{
    if (Open && Chrome)
    {
        const WindowRect rect = CurrentWindowRect();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const EditorChrome::FrameSpec spec = EditorChrome::SpecFor(Style);
        const EditorChrome::FrameRects rects = EditorChrome::FrameLayout(rect.Min, rect.Max, spec);

        dl->PushClipRect(rect.Min, rect.Max, false);
        EditorChrome::DrawFrameEdges(dl, rect.Min, rect.Max, Style, focused);
        if (spec.Rail > 0.0f)
            EditorChrome::DrawHeaderRail(dl, rects.RailMin, rects.RailMax, Style,
                                         EditorChrome::HeaderState{ .Focused = focused });
        dl->PopClipRect();
    }
    ImGui::End();
}
