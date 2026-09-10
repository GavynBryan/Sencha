#pragma once

#include "icons/IconId.h"

#include <imgui.h>

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

// A square mounted button carrying an icon, `size` on a side.
bool IconButton(const char* id, IconId icon, float size, ButtonTone tone);

// The bar-hosted control: a square IconButton whose tone follows `active`
// (a tool or toggle that is on), with a tooltip while hovered. `tooltip` may
// be null. The label form is for a control whose glyph is text (a tool's own
// toolbar control, or a tool with no icon id showing its name).
bool ToolButton(const char* id, IconId icon, const char* tooltip, bool active, float size);
bool ToolButton(const char* id, const char* label, const char* tooltip, bool active, float size);
} // namespace EditorChrome
