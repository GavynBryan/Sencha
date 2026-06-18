#pragma once

#include <imgui.h>

// Draw-list "skin" layer that turns the flat ImGui chrome into a glossy, beveled
// 2003-Winamp-style metal skin: vertical gradients, 1px bevels, and accent glow.
// Every color derives from the EditorUi palette (no literals), so the theme stays
// single-sourced and the ui_color_discipline fitness function is satisfied. The
// look's tunable constants live in the .cpp.
namespace EditorUiSkin
{
// Palette-color math (toward white / toward black, alpha preserved/overridden).
ImVec4 Lighten(const ImVec4& c, float amount);
ImVec4 Darken(const ImVec4& c, float amount);
ImVec4 WithAlpha(const ImVec4& c, float alpha);

// Glossy beveled fill of [mn,mx]: a vertical gradient from a lighter top to a
// darker bottom of `base`, plus a 1px top/left highlight and bottom/right shadow.
// raised = convex (a button at rest); !raised = inset (pressed / a well).
void GradientBevel(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx,
                   const ImVec4& base, bool raised = true);

// A wide glossy "metal band" for the menu / tool / status bars: gradient fill, a
// top highlight, and a thin accent lip along the bottom edge.
void Band(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const ImVec4& base);

// A beveled, gradient icon/text button (InvisibleButton + GradientBevel). `active`
// tints it with the accent; hover adds a cyan glow. Lays out like a normal item
// (advances the cursor; IsItemHovered() after the call works for tooltips).
bool Button(const char* id, const char* label, const ImVec2& size, bool active);

// Subtle top-lit gradient + inner top bevel over the current window's content
// area, giving panels depth. Call once right after a panel's Begin().
void PanelBackdrop();
}
