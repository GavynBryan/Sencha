#include <assets/cook/SceneCookOutput.h>

#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetManifest.h>
#include <core/hash/ContentHash.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>

#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace
{
    std::optional<JsonValue> ParseJsonFile(const std::filesystem::path& path, std::string* error)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            if (error)
                *error = "WriteCookedScene: could not open '" + path.generic_string() + "'";
            return std::nullopt;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();

        JsonParseError parseError;
        std::optional<JsonValue> json = JsonParse(buffer.str(), &parseError);
        if (!json && error)
            *error = "WriteCookedScene: JSON error in '" + path.generic_string()
                + "': " + parseError.Message;
        return json;
    }
} // namespace

bool WriteCookedScene(
    const JsonValue& cookedScene,
    std::span<const std::string> extraRefs,
    const std::function<std::filesystem::path(std::string_view)>& physicalPathFor,
    const std::filesystem::path& idMapPath,
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& cookedScenePath,
    std::string* error)
{
    // Scene refs first (encounter order), then the caller's extra refs, then one
    // level of .smat texture indirection. A single seen-set keeps the manifest
    // free of duplicates while preserving first-seen order.
    std::vector<std::string> paths = CollectAssetPaths(cookedScene);
    std::unordered_set<std::string> seen(paths.begin(), paths.end());

    for (const std::string& ref : extraRefs)
    {
        if (seen.insert(ref).second)
            paths.push_back(ref);
    }

    // Walk .smat indirection over a growing list: a material pulled in by an
    // extra ref still gets its textures collected. Index-based because `paths`
    // grows inside the loop.
    for (std::size_t i = 0; i < paths.size(); ++i)
    {
        if (!paths[i].ends_with(".smat"))
            continue;

        std::optional<JsonValue> smatJson = ParseJsonFile(physicalPathFor(paths[i]), error);
        if (!smatJson)
            return false;

        for (std::string& ref : CollectAssetPaths(*smatJson))
        {
            if (seen.insert(ref).second)
                paths.push_back(std::move(ref));
        }
    }

    // A broken id map must never silently re-mint ids: renames would lose their
    // history. Fail; the committed map is the fix.
    AssetIdMap idMap;
    std::string idMapError;
    if (std::filesystem::exists(idMapPath)
        && !AssetIdMap::LoadFromFile(idMapPath.generic_string(), idMap, &idMapError))
    {
        if (error)
            *error = "WriteCookedScene: bad id map: " + idMapError;
        return false;
    }

    const auto pathIsLive = [&physicalPathFor](std::string_view assetPath) {
        std::error_code existsEc;
        return std::filesystem::exists(physicalPathFor(assetPath), existsEc);
    };

    AssetManifest manifest;
    manifest.Entries.reserve(paths.size());
    for (const std::string& path : paths)
    {
        uint64_t contentHash = 0;
        (void)HashFileContents(physicalPathFor(path).generic_string(), contentHash);
        manifest.Entries.push_back({ idMap.EnsureId(path, contentHash, pathIsLive), path });
    }

    if (idMap.IsDirty() && !idMap.SaveToFile(idMapPath.generic_string()))
    {
        if (error)
            *error = "WriteCookedScene: could not write '" + idMapPath.generic_string() + "'";
        return false;
    }

    if (!WriteAssetManifestFile(manifestPath.generic_string(), manifest))
    {
        if (error)
            *error = "WriteCookedScene: could not write '" + manifestPath.generic_string() + "'";
        return false;
    }

    // The cooked scene: refs the map knows become {"id","path"}; refs it does not
    // know stay plain paths, so the cooked output is never less resolvable than
    // its input.
    std::ofstream out(cookedScenePath, std::ios::trunc);
    if (out.is_open())
        out << JsonStringify(StampAssetRefIds(cookedScene, idMap), /*pretty*/ true);
    if (!out.good())
    {
        if (error)
            *error = "WriteCookedScene: could not write '" + cookedScenePath.generic_string() + "'";
        return false;
    }

    return true;
}
