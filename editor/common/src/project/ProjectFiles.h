#pragma once

#include <core/assets/AssetId.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// A file of a project's content as the editor knows it: the asset path content
// names it by, the id it states, where it is on disk, what kind of file it is --
// its extension, and a data asset's subtype -- and how big and how old it is.
struct ProjectFile
{
    std::string AssetPath = {};
    // As the file states it, in itself or its sidecar; none until it is given
    // one, when it goes by the id of its path (AssetIdOf).
    std::optional<AssetId> StatedId = {};
    std::filesystem::path FilePath = {};
    // Lower case, with its dot: ".smat".
    std::string Extension = {};
    // A data asset's "type"; empty for every other file.
    std::string Subtype = {};
    std::uintmax_t Size = 0;
    std::filesystem::file_time_type Modified{};
};

[[nodiscard]] AssetId AssetIdOf(const ProjectFile& file);

// Every content file under `roots`, sorted by asset path. Cooked output,
// hidden folders and the project's own bookkeeping (asset id maps, per-user
// state, import sidecars) are not content and are left out.
[[nodiscard]] std::vector<ProjectFile> ScanProjectFiles(std::span<const std::filesystem::path> roots);

// The content file `assetPath` names under one of `roots`, as a scan would
// read it; nullopt when no root holds it.
[[nodiscard]] std::optional<ProjectFile> FindProjectFile(std::span<const std::filesystem::path> roots,
                                                         std::string_view assetPath);

// A content file's text, read whole; nullopt when it cannot be read.
[[nodiscard]] std::optional<std::string> ReadProjectFileText(const std::filesystem::path& file);
