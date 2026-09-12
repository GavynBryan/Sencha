#pragma once

#include "icons/IconId.h"

#include <imgui.h>

#include <span>
#include <string_view>

// The chrome's controls: buttons that read as mounted in the chassis rather
// than floating on it. Widget state supplies the tint; the button never knows
// what it does. Dense inputs (text, numbers, checkboxes) stay stock ImGui,
// styled by the palette, so a panel's noise stays low.
namespace EditorChrome
{
enum class ButtonTone
{
    Normal,      // an ordinary action: dark body, steel edge, cyan label
    Active,      // a toggle that is on or an important action: amber
    Destructive, // removes something: red
};

// A mounted button. Hover brightens the edge and adds a faint glow; pressing
// lights the interior. size (0, 0) fits the label. `id` is the ImGui id;
// `label` hides an optional ImGui "##" id suffix.
bool Button(const char* id, const char* label, ImVec2 size, ButtonTone tone);

// A stock combo with a mounted chevron housing. Call EndCombo only when open.
// Width comes from SetNextItemWidth; the label retains its ImGui ID semantics.
bool BeginCombo(const char* label, const char* preview);
void EndCombo();

// A square mounted button carrying an icon, `size` on a side.
bool IconButton(const char* id, IconId icon, float size, ButtonTone tone);

// The bar-hosted control: a square IconButton whose tone follows `active`
// (a tool or toggle that is on), with a tooltip while hovered. `tooltip` may
// be null. The label form is for a control whose glyph is text (a tool's own
// toolbar control, or a tool with no icon id showing its name).
bool ToolButton(const char* id, IconId icon, const char* tooltip, bool active, float size);
bool ToolButton(const char* id, const char* label, const char* tooltip, bool active, float size);

// A tool button painted straight into a draw list at `mn`..`mx` instead of
// mounted as an ImGui item.
//
// It exists for chrome that floats over a viewport: an ImGui item there would
// take the pointer away from the viewport underneath and leave the owning tool
// arguing with the panel over who got the click. Drawn this way there is no
// item at all, and the tool hit-tests the same rect in its own input callback.
// `hot` is that tool's own answer to whether the pointer is over it. The tone
// is the mounted buttons' own, so a confirm reads amber and a cancel red over
// the viewport exactly as they would in a panel.
void DrawIconButton(ImDrawList* dl, ImVec2 mn, ImVec2 mx, IconId icon, ButtonTone tone, bool enabled,
                    bool hot);

// The same button carrying a short label instead of an icon, for a control whose
// glyph is text -- a minus, a plus -- rather than artwork.
void DrawTextButton(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const char* label, ButtonTone tone,
                    bool enabled, bool hot);

// A rotation dial painted into a draw list: the rim it turns on, a dot at each
// stop a snapped turn lands on, and a knob at the current angle. `rim` is the
// already-projected ring, so the chrome never learns what a viewport is.
//
// Painted rather than mounted for the same reason as DrawIconButton: an ImGui
// item floating over a viewport would take the pointer from the geometry
// underneath, and the owning tool hit-tests the same ring in its own callback.
void DrawDial(ImDrawList* dl, std::span<const ImVec2> rim, std::span<const ImVec2> ticks,
              ImVec2 knob, bool hot);

// One slot of a radial menu: a tool button at `Center`, `Size` on a side,
// owning the wedge from `Angle0` to `Angle1` (radians, clockwise from
// straight up). `Label` stands in when the slot has no icon.
struct WheelSlot
{
    ImVec2 Center{};
    float Size = 0.0f;
    float Angle0 = 0.0f;
    float Angle1 = 0.0f;
    IconId Icon = IconId::None;
    const char* Label = nullptr;
    bool Active = false; // the tool that is on
    bool Hot = false;    // the wedge the pointer is in
};

// A radial menu resolved to screen positions: the ring the slots sit on, the
// hub that selects nothing, and the caption line under it all.
struct WheelPaint
{
    ImVec2 Center{};
    float Radius = 0.0f;
    float Hub = 0.0f;
    float CaptionY = 0.0f;
    std::span<const WheelSlot> Slots;
    std::string_view Caption;
    bool CaptionDim = false; // the caption names what is already on, not a choice
};

// A radial menu painted into a draw list as one machined part: petals cut
// into a dark plate, a beveled rim around them, a recessed hub, a ledge under
// the rim for the caption. The wedge is the target, and the paint says so:
// the hot slot's whole petal turns orange. Painted rather than mounted for
// the same reason as the other floating controls: it lives over everything
// and the session that opened it hit-tests the same geometry.
void DrawToolWheel(ImDrawList* dl, const WheelPaint& wheel);
} // namespace EditorChrome
