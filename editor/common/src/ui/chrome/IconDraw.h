#pragma once

#include "ChromeSources.h"
#include "icons/IconId.h"

#include <imgui.h>

// The icon table and the one draw call over it. The table owns the
// procedural-versus-authored choice per id; a control asks for an IconId and
// a tint and never learns which form drew it.
namespace EditorChrome
{
[[nodiscard]] const IconSource& IconSourceFor(IconId id);

// Replaces an id's row, for an authored sprite set registering itself.
void SetIconSource(IconId id, const IconSource& source);

// Restores every row to the built-in procedural and glyph forms.
void ResetIconSources();

// Draws `id` fitted inside [mn, mx] in `tint`, through the fallback chain.
void DrawIcon(ImDrawList* dl, IconId id, ImVec2 mn, ImVec2 mx, ImU32 tint);
} // namespace EditorChrome
