#include "authoring/DocumentLibrary.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "authoring/UiPreviewModel.h"

namespace
{
    constexpr std::string_view kCookedDir = ".cooked";
}

std::filesystem::path DocumentEntry::SourcePath() const
{
    return std::filesystem::path(Root) / RelPath;
}

std::string ScanDataModelName(std::string_view markup)
{
    constexpr std::string_view kAttribute = "data-model=";
    const std::size_t at = markup.find(kAttribute);
    if (at == std::string_view::npos)
        return {};
    std::size_t open = at + kAttribute.size();
    if (open >= markup.size() || (markup[open] != '"' && markup[open] != '\''))
        return {};
    const char quote = markup[open];
    const std::size_t close = markup.find(quote, open + 1);
    if (close == std::string_view::npos)
        return {};
    return std::string(markup.substr(open + 1, close - open - 1));
}

void DocumentLibrary::AddRoot(std::string library, std::string root)
{
    Roots.push_back({ std::move(library), std::move(root) });
}

void DocumentLibrary::Rescan()
{
    Entries.clear();
    for (const LibraryRoot& root : Roots)
    {
        std::error_code ec;
        const std::filesystem::path base(root.Path);
        if (!std::filesystem::is_directory(base, ec))
            continue;
        for (std::filesystem::recursive_directory_iterator it(base, ec), end; it != end; it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }
            if (it->is_directory(ec))
            {
                if (it->path().filename() == kCookedDir)
                    it.disable_recursion_pending();
                continue;
            }
            if (it->path().extension() != ".rml")
                continue;

            DocumentEntry entry;
            entry.Library = root.Library;
            entry.Root = root.Path;
            entry.RelPath = std::filesystem::relative(it->path(), base, ec).generic_string();
            entry.PackagePath = "asset://" + entry.RelPath;
            {
                std::ifstream in(it->path(), std::ios::binary);
                std::ostringstream text;
                text << in.rdbuf();
                entry.ModelName = ScanDataModelName(text.str());
            }
            entry.Cooked = std::filesystem::exists(base / kCookedDir / (entry.RelPath + ".sui"), ec);
            entry.HasPreviewModel = std::filesystem::exists(UiPreviewModel::SidecarFor(it->path()), ec);
            Entries.push_back(std::move(entry));
        }
    }
    std::sort(Entries.begin(), Entries.end(), [](const DocumentEntry& a, const DocumentEntry& b) {
        if (a.Library != b.Library)
            return a.Library < b.Library;
        return a.RelPath < b.RelPath;
    });
}

const DocumentEntry* DocumentLibrary::Find(std::string_view packagePath) const
{
    for (const DocumentEntry& entry : Entries)
        if (entry.PackagePath == packagePath)
            return &entry;
    return nullptr;
}
