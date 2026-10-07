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

std::string Lowered(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// The project's bookkeeping beside its content, rather than content.
bool IsBookkeeping(const std::filesystem::path& file)
{
    const std::string name = file.filename().string();
    return name == "asset_ids.json" || name.ends_with(".user.json") || file.extension() == kAssetSidecarSuffix;
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
    described.Extension = Lowered(file.extension().string());
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

AssetId AssetIdOf(const ProjectFile& file)
{
    return file.StatedId.value_or(AssetIdOfPath(file.AssetPath));
}

std::vector<ProjectFile> ScanProjectFiles(std::span<const std::filesystem::path> roots)
{
    std::vector<ProjectFile> files;
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
            if (it->is_regular_file(ec) && !IsBookkeeping(it->path()))
                files.push_back(Describe(root, it->path()));
        }
    }
    std::ranges::sort(files, {}, &ProjectFile::AssetPath);
    return files;
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
