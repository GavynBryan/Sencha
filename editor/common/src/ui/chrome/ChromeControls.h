#pragma once

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
    Active,      // a toggle that is on: lit cyan interior
    Primary,     // the selection or the important action: amber
    Destructive, // removes something: red
};

// A mounted button. Hover brightens the edge and adds a faint glow; pressing
// lights the interior. size (0, 0) fits the label. `id` is the ImGui id;
// `label` is drawn verbatim (a Font Awesome glyph may lead it) and may not
// carry a "##" id suffix.
bool Button(const char* id, const char* label, ImVec2 size, ButtonTone tone);
} // namespace EditorChrome
