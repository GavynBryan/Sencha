#pragma once

#include <filesystem>
#include <span>
#include <string>
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

// The inverse of ResolveSceneSourceFile: the asset:// path a file is known
// by, or empty when it lives under none of the roots (an unsaved document, a
// file outside the project). Purely lexical -- the file need not exist yet.
[[nodiscard]] inline std::string MakeSceneSourcePath(
    std::span<const std::filesystem::path> roots, const std::filesystem::path& file)
{
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::weakly_canonical(file, ec);
    if (ec)
        return {};
    for (const std::filesystem::path& root : roots)
    {
        const std::filesystem::path canonicalRoot =
            std::filesystem::weakly_canonical(root, ec);
        if (ec)
            continue;
        const std::string relative =
            absolute.lexically_relative(canonicalRoot).generic_string();
        if (relative.empty() || relative.starts_with(".."))
            continue;
        return "asset://" + relative;
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
