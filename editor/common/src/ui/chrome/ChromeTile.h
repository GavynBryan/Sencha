#pragma once

#include "icons/IconId.h"
#include <imgui.h>
#include <string_view>

namespace EditorChrome
{
struct TileSpec
{
    ImTextureID Image = 0;
    float Size = 0.0f;
    std::string_view Label{};
    IconId Badge = IconId::None;
    bool Selected = false;
    bool Disabled = false;
    bool Interactive = true;
};
struct TileResult { bool Clicked; bool Hovered; };

// One item covers the face and optional label. The caller owns its ID scope
// and may attach popups or drag/drop only when Interactive is true.
TileResult Tile(const TileSpec& spec);
} // namespace EditorChrome
