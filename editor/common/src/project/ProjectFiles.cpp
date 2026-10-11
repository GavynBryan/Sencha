#include "project/ProjectFiles.h"

#include "scene_source/Json5Convert.h"
#include "scene_source/Json5Parser.h"

#include <assets/data/DataAssetSubtype.h>
#include <core/assets/AssetIdentity.h>
#include <core/io/FileBytes.h>
#include <core/json/JsonValue.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace
{
constexpr std::string_view kAssetScheme = "asset://";

// The project's bookkeeping beside its content, rather than content.
bool IsBookkeeping(const std::filesystem::path& file)
{
    const std::string name = file.filename().string();
    return name.ends_with(kUserStateSuffix) || file.extension() == kAssetSidecarSuffix;
}



std::string SubtypeOf(const std::filesystem::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return PeekDataAssetSubtype(text);
}

ProjectFile Describe(const std::filesystem::path& root, const std::filesystem::path& file)
{
    std::error_code ec;
    ProjectFile described;
    described.FilePath = file;
    described.AssetPath = std::string(kAssetScheme) + std::filesystem::relative(file, root, ec).generic_string();
    described.Extension = ExtensionOf(file);
    described.Size = std::filesystem::file_size(file, ec);
    described.Modified = std::filesystem::last_write_time(file, ec);
    if (described.Extension == ".sdata")
        described.Subtype = SubtypeOf(file);
    described.StatedId = ReadStatedId(file);
    return described;
}
} // namespace

std::optional<AssetId> ReadStatedId(const std::filesystem::path& file)
{
    const std::optional<std::string> text = ReadProjectFileText(IdStatementOf(file));
    const std::optional<Json5Value> root = text.has_value() ? Json5Parse(*text) : std::nullopt;
    return root.has_value() ? StatedAssetId(Json5ToJson(*root)) : std::nullopt;
}

std::string ExtensionOf(const std::filesystem::path& file)
{
    std::string extension = file.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

AssetId AssetIdOf(const ProjectFile& file)
{
    return file.StatedId.value_or(AssetIdOfPath(file.AssetPath));
}

namespace
{
// Every regular file under `roots` outside hidden folders, with the root it is under.
template <typename Visit>
void ForEachFile(std::span<const std::filesystem::path> roots, Visit&& visit)
{
    for (const std::filesystem::path& root : roots)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec))
            continue;
        for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec))
        {
            const std::string name = it->path().filename().string();
            if (name.starts_with('.'))
            {
                if (it->is_directory(ec))
                    it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(ec))
                visit(root, it->path());
        }
    }
}
} // namespace

std::vector<ProjectFile> ScanProjectFiles(std::span<const std::filesystem::path> roots)
{
    std::vector<ProjectFile> files;
    ForEachFile(roots, [&](const std::filesystem::path& root, const std::filesystem::path& file) {
        if (!IsBookkeeping(file))
            files.push_back(Describe(root, file));
    });
    std::ranges::sort(files, {}, &ProjectFile::AssetPath);
    return files;
}

std::vector<std::filesystem::path> ScanOrphanedSidecars(std::span<const std::filesystem::path> roots)
{
    std::vector<std::filesystem::path> orphans;
    ForEachFile(roots, [&](const std::filesystem::path&, const std::filesystem::path& file) {
        std::error_code ec;
        if (file.extension() == kAssetSidecarSuffix && !std::filesystem::exists(std::filesystem::path(file).replace_extension(), ec))
            orphans.push_back(file);
    });
    std::ranges::sort(orphans);
    return orphans;
}

std::optional<ProjectFile> FindProjectFile(std::span<const std::filesystem::path> roots, std::string_view assetPath)
{
    if (!assetPath.starts_with(kAssetScheme))
        return std::nullopt;
    const std::filesystem::path relative(assetPath.substr(kAssetScheme.size()));
    for (const std::filesystem::path& root : roots)
    {
        std::error_code ec;
        const std::filesystem::path file = root / relative;
        if (std::filesystem::is_regular_file(file, ec))
            return Describe(root, file);
    }
    return std::nullopt;
}

std::optional<std::string> ReadProjectFileText(const std::filesystem::path& file)
{
    std::vector<std::byte> bytes;
    if (!ReadFileBytes(file, bytes))
        return std::nullopt;
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

AssetRef CurrentInProject(std::span<const std::filesystem::path> roots, AssetRef ref)
{
    if (const std::optional<ProjectFile> hinted = ref.Path.empty() ? std::nullopt : FindProjectFile(roots, ref.Path))
        if (!ref.Id.IsValid() || AssetIdOf(*hinted) == ref.Id)
        {
            ref.Id = AssetIdOf(*hinted);
            return ref;
        }
    if (ref.Id.IsValid())
        for (const ProjectFile& file : ScanProjectFiles(roots))
            if (AssetIdOf(file) == ref.Id)
            {
                ref.Path = file.AssetPath;
                return ref;
            }
    return ref;
}
