#pragma once

#include <filesystem>
#include <span>
#include <string_view>

// The two path conversions everything around scene sources performs: an
// asset:// reference to the file it names under the content roots, and the
// stem a source is called by where it appears as one thing (a browser cell, a
// nameless placement's row). One definition, because three call sites already
// grew their own.

[[nodiscard]] inline std::filesystem::path ResolveSceneSourceFile(
    std::span<const std::filesystem::path> roots, std::string_view assetPath)
{
    constexpr std::string_view prefix = "asset://";
    if (assetPath.rfind(prefix, 0) != 0)
        return {};
    const std::string_view relative = assetPath.substr(prefix.size());
    for (const std::filesystem::path& root : roots)
    {
        std::error_code ec;
        if (std::filesystem::path candidate = root / relative;
            std::filesystem::is_regular_file(candidate, ec))
        {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] inline std::string_view SceneSourceStem(std::string_view assetPath)
{
    const std::size_t slash = assetPath.find_last_of('/');
    std::string_view leaf =
        slash == std::string_view::npos ? assetPath : assetPath.substr(slash + 1);
    if (leaf.ends_with(".sscene"))
        leaf.remove_suffix(sizeof(".sscene") - 1);
    return leaf;
}
