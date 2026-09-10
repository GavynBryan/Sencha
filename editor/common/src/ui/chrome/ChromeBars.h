#pragma once

#include <imgui.h>

// The bar hosts (menu, toolbar, status bar) and the modules
// mounted in them. A bar is a metal band with a lit lip on the edge facing the
// workspace; a module is a recessed bay a group of related controls sits in,
// so a toolbar reads as installed rather than as buttons floating on a strip.
namespace EditorChrome
{
enum class BarEdge
{
    Bottom, // the lip runs along the bottom edge (a top bar)
    Top,    // along the top edge (a bottom bar)
};

// The band across [mn, mx]. Call first thing inside the bar's window.
void BarBackdrop(ImDrawList* dl, ImVec2 mn, ImVec2 mx, BarEdge lipEdge);

// The edge length bar buttons are laid out at, derived from the current font
// so bars and the controls tools draw into them agree without passing sizes.
[[nodiscard]] float BarButtonSize();

enum class LedState { Off, On, Alert };

// Single items suitable for both menu bars and ordinary horizontal flows.
void Divider();
void Readout(const char* label, const char* value);
void Readout(const char* label, const char* value, LedState state);

// A module: the controls issued between construction and destruction share
// one recessed bay, drawn under them when the scope ends. Lays out as one
// ImGui group; put SameLine between modules. An empty module draws nothing.
class ModuleScope
{
public:
    explicit ModuleScope(const char* id);
    ~ModuleScope();

    ModuleScope(const ModuleScope&) = delete;
    ModuleScope& operator=(const ModuleScope&) = delete;

    // Lights the bay's edge, for the module whose function is running (the
    // transport while a session plays).
    void SetActive(bool active) { Active = active; }

private:
    ImDrawList* Dl;
    ImDrawListSplitter Splitter;
    bool Active = false;
};
} // namespace EditorChrome
