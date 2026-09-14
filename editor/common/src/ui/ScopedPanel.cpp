#include "ScopedPanel.h"

#include "EditorUiStyle.h"
#include "chrome/ChromeFrame.h"
#include "chrome/ChromeGeometry.h"
#include "chrome/ChromeHeader.h"
#include "chrome/ChromeOrnaments.h"

#include <array>

namespace
{
// The rect the frame owns: the window below its title bar or dock tab (a
// docked window's rect includes the tab bar ImGui draws for it). The content
// region's top is that boundary plus the padding, less the scroll, which is
// added back so the frame stays put while the body scrolls under it.
ImVec2 FrameMin()
{
    const ImVec2 pos = ImGui::GetWindowPos();
    return ImVec2(pos.x, pos.y + ImGui::GetWindowContentRegionMin().y + ImGui::GetScrollY()
                             - ImGui::GetStyle().WindowPadding.y);
}

ImVec2 FrameMax()
{
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    return ImVec2(pos.x + size.x, pos.y + size.y);
}
}

ScopedPanel::ScopedPanel(std::string_view title, bool* open, PanelStyle style, ImGuiWindowFlags flags)
    : Style(style)
{
    // The padding keeps widgets inside the ring; ImGui reads it at Begin.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, EditorChrome::ChromeSpecFor(style).ContentPadding);
    Open = ImGui::Begin(title.data(), open, flags);
    ImGui::PopStyleVar();
    if (!Open)
        return;

    Min = FrameMin();
    Max = FrameMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // ImGui clips the body to the window's inner rect; the chrome spans the
    // whole frame, so it draws under its own clip.
    dl->PushClipRect(Min, Max, false);
    EditorChrome::DrawFrameBase(dl, Min, Max, style);
    dl->PopClipRect();

    // Content starts under the rail; it scrolls beneath it later.
    const EditorChrome::FrameSpec spec = EditorChrome::ChromeSpecFor(style).Frame;
    if (spec.Rail > 0.0f)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + spec.Rail + EditorUi::Px(2.0f));
}

ScopedPanel::~ScopedPanel()
{
    if (Open)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const EditorChrome::PanelChromeSpec chrome = EditorChrome::ChromeSpecFor(Style);
        const EditorChrome::FrameSpec& spec = chrome.Frame;
        const EditorChrome::FrameRects rects = EditorChrome::FrameLayout(Min, Max, spec);

        const EditorUi::ChromeMetrics& m = EditorUi::Metrics;
        const float gap = EditorUi::Px(4.0f);
        const float vent = EditorUi::Px(m.VentLength);
        const float slash = EditorUi::Px(m.VentLength * 0.5f);
        const float led = EditorUi::Px(m.ScrewRadius) * 2.0f;
        std::array<EditorChrome::OrnamentSlot, 8> slots{};
        int placed = 0;
        float railOrnaments = 0.0f;

        // Where the hardware mounts is the composition's business, not this
        // scope's: a well-mounted frame scales its ornaments with its size,
        // while a ring-mounted one puts them in the metal because its body
        // covers the well.
        if (chrome.Mount == EditorChrome::OrnamentMount::Ring)
        {
            placed = EditorChrome::LayoutRingOrnaments(Min, Max, spec.Border + spec.Recess, spec.Chamfer,
                                                       EditorUi::Px(m.ScrewRadius) * 1.35f, vent,
                                                       EditorUi::Px(m.VentLength * 0.6f), gap, slots);
        }
        else
        {
            // Ornament density follows the panel's size: a small panel keeps a
            // plain frame, a large one carries screws and a vented rail.
            const EditorChrome::OrnamentTier tier = EditorChrome::TierFor(
                ImVec2(Max.x - Min.x, Max.y - Min.y), EditorUi::Px(m.OrnamentMediumMin), EditorUi::Px(m.OrnamentLargeMin));
            placed = EditorChrome::LayoutOrnaments(rects, tier, EditorUi::Px(m.ScrewRadius), vent, slash, gap, slots);
            railOrnaments = EditorChrome::RailOrnamentWidth(tier, vent, slash, led, gap);
        }

        dl->PushClipRect(Min, Max, false);
        EditorChrome::DrawFrameEdges(dl, Min, Max, Style, focused);
        if (spec.Rail > 0.0f)
            EditorChrome::DrawHeaderRail(dl, rects.RailMin, rects.RailMax, Style,
                                         EditorChrome::HeaderState{ .Focused = focused }, railOrnaments);
        const ImU32 accent = ImGui::GetColorU32(focused ? EditorUi::AccentHover : EditorUi::Accent);
        // The LED and the light strips are the ornaments that read state: lit
        // while the panel is being worked in, banked otherwise.
        const ImU32 ledTint = ImGui::GetColorU32(focused ? EditorUi::AccentHover : EditorUi::Darken(EditorUi::Accent, 0.55f));
        const ImU32 stripTint = ImGui::GetColorU32(focused ? EditorUi::SelectedOutline
                                                           : EditorUi::Darken(EditorUi::SelectedOutline, 0.55f));
        for (int i = 0; i < placed; ++i)
        {
            const EditorChrome::OrnamentSlot& slot = slots[static_cast<std::size_t>(i)];
            ImU32 tint = accent;
            if (slot.Kind == EditorChrome::OrnamentKind::StatusLed)
                tint = ledTint;
            else if (slot.Kind == EditorChrome::OrnamentKind::LightStrip)
                tint = stripTint;
            EditorChrome::DrawOrnament(dl, slot.Kind, slot.Min, slot.Max, tint);
        }
        dl->PopClipRect();
    }
    ImGui::End();
}
