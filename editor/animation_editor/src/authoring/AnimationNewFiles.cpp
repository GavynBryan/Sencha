#include "authoring/AnimationNewFiles.h"

#include "data/DataAssetFiles.h"

#include <core/assets/AssetRegistry.h>
#include <core/json/JsonFormat.h>

#include <format>
#include <fstream>

bool WriteAnimationNewDocuments(const std::filesystem::path& root, AssetRegistry& registry,
                                const std::vector<AnimationNewDocument>& documents, std::string& error)
{
    for (const AnimationNewDocument& document : documents)
        if (DataAssetPathTaken(registry, root / document.RelativePath, "asset://" + document.RelativePath))
        {
            error = std::format("'{}' already exists; choose another name.", document.RelativePath);
            return false;
        }
    for (const AnimationNewDocument& document : documents)
    {
        if (!WriteAnimationNewFile(root, document, 4, error))
            return false;
        RegisterDataAssetFile(registry, "asset://" + document.RelativePath, root / document.RelativePath);
    }
    return true;
}

bool WriteAnimationNewFile(const std::filesystem::path& root, const AnimationNewDocument& document, int indent,
                           std::string& error)
{
    const std::filesystem::path file = root / document.RelativePath;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    out << JsonFormat(document.Root, indent) << "\n";
    if (out.good())
        return true;
    error = std::format("Could not write '{}'.", document.RelativePath);
    return false;
}
