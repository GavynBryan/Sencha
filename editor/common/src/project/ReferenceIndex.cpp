#include "project/ReferenceIndex.h"

#include "scene_source/Json5Convert.h"
#include "scene_source/Json5Parser.h"

#include <core/assets/AssetManifest.h>

#include <algorithm>
#include <array>
#include <unordered_set>

namespace
{
// The content documents that can name other files; everything else is read by
// whatever imports it and names nothing.
constexpr std::array<std::string_view, 4> kDocuments = { ".smat", ".sscene", ".sdata", ".json" };

// A scene source cooks under its own stem (`DeriveDocumentCookPaths`), so a
// reference to the cooked form names the source.
constexpr std::string_view kCookedScene = ".smap";
constexpr std::string_view kSceneSource = ".sscene";

bool IsDocument(const ProjectFile& file)
{
    return std::ranges::find(kDocuments, file.Extension) != kDocuments.end();
}
} // namespace

std::string ReferencedFile(std::string_view reference)
{
    reference = reference.substr(0, reference.find('#'));
    if (reference.ends_with(kCookedScene))
        return std::string(reference.substr(0, reference.size() - kCookedScene.size())) + std::string(kSceneSource);
    return std::string(reference);
}

std::vector<std::string> ReferencesIn(std::string_view text)
{
    const std::optional<Json5Value> document = Json5Parse(text);
    return document.has_value() ? CollectAssetPaths(Json5ToJson(*document)) : std::vector<std::string>{};
}

void ReferenceIndex::Refresh(std::span<const ProjectFile> files, std::span<const ProjectFile> elsewhere,
                             const ReadText& read)
{
    std::unordered_set<std::string_view> present;
    for (const ProjectFile& file : files)
    {
        present.insert(file.AssetPath);
        if (!IsDocument(file))
            continue;
        const auto known = Documents.find(file.AssetPath);
        if (known != Documents.end() && known->second.Size == file.Size && known->second.Modified == file.Modified)
            continue;
        const std::optional<std::string> text = read(file.FilePath);
        Documents[file.AssetPath] = { .Size = file.Size,
                                      .Modified = file.Modified,
                                      .References = text.has_value() ? ReferencesIn(*text) : std::vector<std::string>{} };
    }
    std::erase_if(Documents, [&](const auto& entry) { return !present.contains(entry.first); });

    std::unordered_set<std::string_view> found = present;
    for (const ProjectFile& file : elsewhere)
        found.insert(file.AssetPath);

    UsesOf.clear();
    UsersOf.clear();
    MissingOf.clear();
    for (const auto& [user, document] : Documents)
        for (const std::string& reference : document.References)
        {
            std::string named = ReferencedFile(reference);
            if (!found.contains(named))
                MissingOf[user].push_back(reference);
            else if (named != user)
            {
                UsersOf[named].push_back(user);
                UsesOf[user].push_back(std::move(named));
            }
        }
    for (auto* table : { &UsesOf, &UsersOf, &MissingOf })
        for (auto& [name, list] : *table)
        {
            std::ranges::sort(list);
            const auto repeated = std::ranges::unique(list);
            list.erase(repeated.begin(), repeated.end());
        }
}

std::span<const std::string> ReferenceIndex::Find(const ByName<std::vector<std::string>>& table,
                                                  std::string_view assetPath)
{
    const auto found = table.find(assetPath);
    return found != table.end() ? std::span<const std::string>(found->second) : std::span<const std::string>{};
}

std::span<const std::string> ReferenceIndex::Uses(std::string_view assetPath) const
{
    return Find(UsesOf, assetPath);
}

std::span<const std::string> ReferenceIndex::UsedBy(std::string_view assetPath) const
{
    return Find(UsersOf, assetPath);
}

std::span<const std::string> ReferenceIndex::Missing(std::string_view assetPath) const
{
    return Find(MissingOf, assetPath);
}
