#pragma once

#include <string_view>

// A path's last segment -- "hero.rig.sdata" of "asset://anim/hero.rig.sdata" --
// what a document or asset is called where there is room for one name.
[[nodiscard]] constexpr std::string_view PathLeaf(std::string_view path)
{
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}
