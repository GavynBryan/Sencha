#include "project/AssetQuery.h"

#include "project/AssetKinds.h"
#include "text/PathLeaf.h"
#include "text/TextFilterMatch.h"

#include <core/assets/AssetPath.h>

#include <algorithm>
#include <optional>

namespace
{
// A word found only in the folders above a file ranks after one in its name.
constexpr int kPathRank = 3;

std::vector<std::string_view> Words(std::string_view text)
{
    std::vector<std::string_view> words;
    while (!text.empty())
    {
        const std::size_t start = text.find_first_not_of(' ');
        if (start == std::string_view::npos)
            break;
        text.remove_prefix(start);
        const std::size_t end = text.find(' ');
        words.push_back(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end);
    }
    return words;
}

bool KindMatches(const ProjectFile& file, std::string_view wanted)
{
    if (wanted.empty())
        return true;
    const AssetKind kind = KindOf(file);
    return TextFilterRank(kind.Category, wanted) == 0 || TextFilterRank(kind.Label, wanted) == 0;
}

// How well `words` name `file`, best 0; nullopt when one of them does not.
std::optional<int> RankOf(const ProjectFile& file, const std::vector<std::string_view>& words)
{
    int worst = 0;
    for (const std::string_view word : words)
    {
        std::optional<int> rank = TextFilterRank(NameOf(file), word);
        if (!rank.has_value())
            if (const std::optional<int> inPath = TextFilterRank(ContentRelativePath(file.AssetPath), word))
                rank = *inPath + kPathRank;
        if (!rank.has_value())
            return std::nullopt;
        worst = std::max(worst, *rank);
    }
    return worst;
}
} // namespace

std::string FolderOf(const ProjectFile& file)
{
    const std::string_view path = ContentRelativePath(file.AssetPath);
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string{} : std::string(path.substr(0, slash));
}

std::string_view NameOf(const ProjectFile& file)
{
    return PathLeaf(file.AssetPath);
}

std::vector<const ProjectFile*> FindAssets(std::span<const ProjectFile> files, const AssetQuery& query)
{
    std::vector<std::string_view> words;
    std::string_view kind = query.Category;
    std::string_view folder;
    for (const std::string_view word : Words(query.Text))
    {
        if (word.starts_with("t:"))
            kind = word.substr(2);
        else if (word.starts_with("in:"))
            folder = word.substr(3);
        else
            words.push_back(word);
    }

    struct Found
    {
        const ProjectFile* File;
        int Rank;
    };
    std::vector<Found> found;
    for (const ProjectFile& file : files)
    {
        const std::string in = FolderOf(file);
        if (!query.Folder.empty() && in != query.Folder && !in.starts_with(query.Folder + "/"))
            continue;
        if (!folder.empty() && !TextFilterMatch(in, folder))
            continue;
        if (!KindMatches(file, kind))
            continue;
        if (const std::optional<int> rank = RankOf(file, words))
            found.push_back({ &file, *rank });
    }
    std::ranges::stable_sort(found, [&query](const Found& a, const Found& b) {
        if (a.Rank != b.Rank || query.KeepOrder)
            return a.Rank < b.Rank;
        return NameOf(*a.File) < NameOf(*b.File);
    });
    std::vector<const ProjectFile*> ordered;
    ordered.reserve(found.size());
    for (const Found& entry : found)
        ordered.push_back(entry.File);
    return ordered;
}

std::vector<std::string> FoldersOf(std::span<const ProjectFile> files)
{
    std::vector<std::string> folders;
    for (const ProjectFile& file : files)
    {
        std::string folder = FolderOf(file);
        while (!folder.empty())
        {
            folders.push_back(folder);
            const std::size_t slash = folder.rfind('/');
            folder = slash == std::string::npos ? std::string{} : folder.substr(0, slash);
        }
    }
    std::ranges::sort(folders);
    folders.erase(std::unique(folders.begin(), folders.end()), folders.end());
    return folders;
}
