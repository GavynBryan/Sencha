#pragma once

#include "project/ProjectFiles.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The file a reference names: a part of a file (`model.blend#anim:Walk`) is the
// file, and a cooked scene (.smap) is the scene source cooked under its stem.
[[nodiscard]] std::string ReferencedFile(std::string_view reference);

// Every reference a content document makes, in either form (AssetRefCodec), in
// the order written; none when the text is not JSON or JSON5.
[[nodiscard]] std::vector<AssetRef> ReferencesIn(std::string_view text);

//=============================================================================
// ReferenceIndex
//
// Which of the project's content files name which: what a file uses, what
// uses it, and what it names that is nowhere -- in the project or in content
// mounted beside it (the engine's). A reference finds its file by the id it
// names, else by its path. A refresh reads again only the documents whose size
// or time changed since they were read.
//=============================================================================
class ReferenceIndex
{
public:
    using ReadText = std::function<std::optional<std::string>(const std::filesystem::path&)>;

    void Refresh(std::span<const ProjectFile> files, std::span<const ProjectFile> elsewhere, const ReadText& read);

    // Each sorted by asset path.
    [[nodiscard]] std::span<const std::string> Uses(std::string_view assetPath) const;
    [[nodiscard]] std::span<const std::string> UsedBy(std::string_view assetPath) const;
    // References whose file is nowhere, by the path they name (by id when they
    // name none).
    [[nodiscard]] std::span<const std::string> Missing(std::string_view assetPath) const;
    [[nodiscard]] bool IsUnused(std::string_view assetPath) const { return UsedBy(assetPath).empty(); }

private:
    struct NameHash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view name) const { return std::hash<std::string_view>{}(name); }
    };
    template <typename T>
    using ByName = std::unordered_map<std::string, T, NameHash, std::equal_to<>>;

    struct Document
    {
        std::uintmax_t Size = 0;
        std::filesystem::file_time_type Modified{};
        std::vector<AssetRef> References;
    };

    [[nodiscard]] static std::span<const std::string> Find(const ByName<std::vector<std::string>>& table,
                                                           std::string_view assetPath);

    ByName<Document> Documents;
    ByName<std::vector<std::string>> UsesOf;
    ByName<std::vector<std::string>> UsersOf;
    ByName<std::vector<std::string>> MissingOf;
};
