#pragma once

#include "icons/IconId.h"

#include <imgui.h>

// The one draw call for an icon: a control asks for an IconId and a tint and
// never learns what drew it.
namespace EditorChrome
{
// Draws `id` fitted inside [mn, mx] in `tint`.
void DrawIcon(ImDrawList* dl, IconId id, ImVec2 mn, ImVec2 mx, ImU32 tint);
} // namespace EditorChrome
