#include "scene_source/SceneSourceCache.h"

#include "scene_source/SceneSourcePaths.h"

#include <fstream>
#include <sstream>

const SceneSourceDocument* SceneSourceCache::Find(std::string_view assetPath)
{
    LastError_.clear();

    const std::filesystem::path file = ResolveSceneSourceFile(Roots, assetPath);
    if (file.empty())
    {
        LastError_ = "source '" + std::string(assetPath)
            + "' was not found under any content root";
        Entries.erase(std::string(assetPath));
        return nullptr;
    }

    std::error_code ec;
    const auto writeTime = std::filesystem::last_write_time(file, ec);
    const auto size = std::filesystem::file_size(file, ec);

    const std::string key(assetPath);
    if (const auto found = Entries.find(key);
        found != Entries.end()
        && found->second.WriteTime == writeTime && found->second.Size == size)
    {
        if (found->second.Valid)
            return &found->second.Document;
        LastError_ = "source '" + key + "' failed to parse previously";
        return nullptr;
    }

    Entry entry;
    entry.WriteTime = writeTime;
    entry.Size = size;

    std::ifstream stream(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();

    std::string parseError;
    if (std::optional<SceneSourceDocument> parsed =
            ParseSceneSource(buffer.str(), &parseError))
    {
        entry.Document = std::move(*parsed);
        entry.Valid = true;
    }
    else
    {
        LastError_ = "source '" + key + "': " + parseError;
    }

    const auto [slot, inserted] = Entries.insert_or_assign(key, std::move(entry));
    return slot->second.Valid ? &slot->second.Document : nullptr;
}
