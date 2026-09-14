#pragma once

#include "ChromeGeometry.h"
#include "ui/EditorUiStyle.h"

#include <imgui.h>

#include <span>

// The bar hosts (menu, toolbar, status bar) and the modules mounted in them.
// A bar is a fabricated band: a metal rim along each edge, a recessed channel
// between them terminated by a slanted cap at each end, and a lit lip on the
// edge facing the workspace. Controls are seated on the channel's lane, and a
// module is a recessed bay a group of related controls sits in, so a toolbar
// reads as installed rather than as buttons floating on a strip.
namespace EditorChrome
{
enum class BarEdge
{
    Bottom, // the lip runs along the bottom edge (a top bar)
    Top,    // along the top edge (a bottom bar)
};

// The band across [mn, mx]: gradient, bevel, and lip, with no structure over
// it. For a bar too short to carry rims and a channel. Call first thing inside
// the bar's window.
void BarBackdrop(ImDrawList* dl, ImVec2 mn, ImVec2 mx, BarEdge lipEdge);

// A bar's channel surface, already resolved: the finish a theme asked for and,
// when it asked for a texture, the image to tile and the tint to modulate it
// by. Resolving a theme's path into an image is somebody else's job -- by the
// time a bar paints, this is a value. A zero texture paints Solid, so a missing
// asset needs no special case here.
struct BarSurface
{
    EditorUi::BarFinish Finish = EditorUi::BarFinish::Solid;
    ImTextureID Texture = 0;
    ImVec2 TextureSize{};
    ImU32 Tint = IM_COL32_WHITE;
};

// The full chassis across [mn, mx] for controls `itemHeight` tall: the band,
// then the recessed channel, rims, seam, and end caps over it. Returns the
// rects the host seats its content in; seat the row at LaneMin. Call first
// thing inside the bar's window.
//
// The surface is paint only: every rect this returns is the same whichever
// finish a theme asked for.
BarRects BarFrame(ImDrawList* dl, ImVec2 mn, ImVec2 mx, BarEdge lipEdge, float itemHeight,
                  const BarSurface& surface);

// The height a bar needs to give `itemHeight` controls their clearance, and
// the distance from a bar's edge to its lane. Every bar derives its height
// from BarHeight, so the bars agree without passing sizes between them.
[[nodiscard]] float BarHeight(float itemHeight);
[[nodiscard]] float BarLaneInset();

// The edge length bar buttons are laid out at, derived from the current font
// so bars and the controls tools draw into them agree without passing sizes.
[[nodiscard]] float BarButtonSize();

// A recessed bay cut into a bar's channel, lit along its rim when active.
// ModuleScope paints one around its group; a caller that already knows the
// geometry (a strip of menus, say) mounts one directly.
void DrawBay(ImDrawList* dl, ImVec2 mn, ImVec2 mx, bool active);

// Technical markings in a run of the lane the controls left free, between
// fromX and toX. Draws nothing in a run too short to carry them.
void BarMarkings(ImDrawList* dl, const BarRects& bar, float fromX, float toX);

// The width a run of horizontal ImGui menus takes, for a caller placing the
// strip itself. Mirrors the spacing BeginMenu adds around each label, so it is
// pinned by a test against the ImGui the editor builds with.
[[nodiscard]] float MenuBarStripWidth(std::span<const char* const> labels);

enum class LedState { Off, On, Alert };

// Single items suitable for both menu bars and ordinary horizontal flows.
void Divider();
void Readout(const char* label, const char* value);
void Readout(const char* label, const char* value, LedState state);
// The width Readout will take, for a caller that right-aligns it.
[[nodiscard]] float ReadoutWidth(const char* label, const char* value, LedState state);

// A module: the controls issued between construction and destruction share
// one recessed bay, drawn under them when the scope ends. Lays out as one
// ImGui group; put SameLine between modules. An empty module draws nothing and
// takes no space, so a host can open one unconditionally.
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
    ImVec2 Origin;
    ImDrawListSplitter Splitter;
    bool Active = false;
};
} // namespace EditorChrome
