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

// Whether a file of this extension can name other files: the content documents.
// Everything else is read by whatever imports it and names nothing.
[[nodiscard]] bool NamesOtherFiles(std::string_view extension);
// The file a reference names: a part of a file (`model.blend#anim:Walk`) is the
// file, and a cooked scene (.smap) is the scene source cooked under its stem.
[[nodiscard]] std::string ReferencedFile(std::string_view reference);
// The cooked scene a scene source cooks to, which references may name instead.
[[nodiscard]] std::string CookedSceneOf(std::string_view scenePath);

// Every reference a content document makes, in either form (AssetRefCodec), in
// the order written; none when the text is not JSON or JSON5.
[[nodiscard]] std::vector<AssetRef> ReferencesIn(std::string_view text);

//=============================================================================
// ReferenceIndex
// Which content files name which, and what each names that exists nowhere.
// A reference resolves by id, else by path; a refresh rereads only changed files.
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
