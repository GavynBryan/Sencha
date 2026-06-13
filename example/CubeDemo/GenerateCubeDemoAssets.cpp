// Build-time dev-asset generator for CubeDemo (docs/assets/pipeline.md,
// Stages 1, 3, and 4e). Four jobs, all seeds of the Stage 4 cook step:
//
//   1. Writes the demo's cube mesh as a real .smesh file so the demo loads
//      it through the file path like shipped content would. Generating at
//      build time (rather than committing the binary) keeps the bytes in
//      sync with StaticMeshVertex when the format version moves.
//   2. Derives the scene's asset manifest — the transitive closure of every
//      asset:// reference in the scene plus, for each referenced .smat, the
//      texture refs inside it. Derived data, never authored (Decision D).
//   3. Maintains the persisted asset id map (Decision A): every manifest
//      path gets a stable id at first sight; renames keep theirs via the
//      map's content hashes. The map at <assets-root>/asset_ids.json is the
//      committed identity record — this tool only appends and rehashes.
//   4. Emits the cooked scene, <scene-stem>.cooked.json: the authored scene
//      with every known asset ref stamped {"id", "path"} so the runtime
//      resolves by id with the path as fallback. The authored scene is
//      never modified — it stays the editor's round-trip format.
//
// Usage: GenerateCubeDemoAssets <output-assets-root> <scene-file>
//   The manifest is written next to the scene file as
//   <scene-stem>.manifest.json.

#include <assets/static_mesh/StaticMeshSerializer.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetManifest.h>
#include <core/hash/ContentHash.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/ConsoleLogSink.h>
#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/StaticMeshPrimitives.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>

namespace
{
    constexpr std::string_view kAssetPrefix = "asset://";

    std::optional<JsonValue> ParseJsonFile(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            std::fprintf(stderr, "GenerateCubeDemoAssets: could not open '%s'\n",
                         path.generic_string().c_str());
            return std::nullopt;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();

        JsonParseError parseError;
        std::optional<JsonValue> json = JsonParse(buffer.str(), &parseError);
        if (!json)
        {
            std::fprintf(stderr, "GenerateCubeDemoAssets: JSON error in '%s' at %zu: %s\n",
                         path.generic_string().c_str(), parseError.Position,
                         parseError.Message.c_str());
        }
        return json;
    }

    // Maps "asset://x/y.ext" to "<assetsRoot>/x/y.ext" — the inverse of the
    // scanner's MakeVirtualAssetPath.
    std::filesystem::path PhysicalPathFor(const std::filesystem::path& assetsRoot,
                                          std::string_view assetPath)
    {
        return assetsRoot / std::string(assetPath.substr(kAssetPrefix.size()));
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: GenerateCubeDemoAssets <output-assets-root> <scene-file>\n");
        return 1;
    }

    const std::filesystem::path outRoot{ argv[1] };
    const std::filesystem::path scenePath{ argv[2] };

    std::error_code ec;
    std::filesystem::create_directories(outRoot / "meshes/dev", ec);
    if (ec)
    {
        std::fprintf(stderr, "GenerateCubeDemoAssets: could not create '%s': %s\n",
                     (outRoot / "meshes/dev").generic_string().c_str(), ec.message().c_str());
        return 1;
    }

    LoggingProvider logging;
    logging.AddSink<ConsoleLogSink>();

    StaticMeshSerializer serializer(logging);
    const std::string meshPath = (outRoot / "meshes/dev/cube.smesh").generic_string();
    if (!serializer.WriteToFile(meshPath, StaticMeshPrimitives::BuildCube(1.0f)))
        return 1;

    // Manifest: scene refs first, then one level of .smat indirection (the
    // only ref-bearing payload that exists yet — the same walk covers both
    // because CollectAssetPaths is schema-agnostic).
    std::optional<JsonValue> sceneJson = ParseJsonFile(scenePath);
    if (!sceneJson)
        return 1;

    std::vector<std::string> paths = CollectAssetPaths(*sceneJson);

    std::unordered_set<std::string> seen(paths.begin(), paths.end());
    const std::size_t sceneRefCount = paths.size();
    for (std::size_t i = 0; i < sceneRefCount; ++i)
    {
        const std::string& path = paths[i];
        if (!path.ends_with(".smat"))
            continue;

        std::optional<JsonValue> smatJson = ParseJsonFile(PhysicalPathFor(outRoot, path));
        if (!smatJson)
            return 1;

        for (std::string& ref : CollectAssetPaths(*smatJson))
        {
            if (seen.insert(ref).second)
                paths.push_back(std::move(ref));
        }
    }

    // Stable ids (Decision A / Stage 4e): load the persisted map, give every
    // manifest path an id — first sight mints, renames inherit via content
    // hash — and save only when something changed.
    const std::filesystem::path idMapPath = outRoot / kAssetIdMapFileName;
    AssetIdMap idMap;
    std::string idMapError;
    if (std::filesystem::exists(idMapPath)
        && !AssetIdMap::LoadFromFile(idMapPath.generic_string(), idMap, &idMapError))
    {
        // A broken id map must never silently re-mint ids: renames would
        // lose their history. Fail the build; the committed map is the fix.
        std::fprintf(stderr, "GenerateCubeDemoAssets: bad id map: %s\n", idMapError.c_str());
        return 1;
    }

    const auto pathIsLive = [&outRoot](std::string_view assetPath) {
        std::error_code existsEc;
        return std::filesystem::exists(PhysicalPathFor(outRoot, assetPath), existsEc);
    };

    AssetManifest manifest;
    manifest.Entries.reserve(paths.size());
    for (const std::string& path : paths)
    {
        uint64_t contentHash = 0;
        (void)HashFileContents(PhysicalPathFor(outRoot, path).generic_string(), contentHash);
        manifest.Entries.push_back({ idMap.EnsureId(path, contentHash, pathIsLive), path });
    }

    if (idMap.IsDirty() && !idMap.SaveToFile(idMapPath.generic_string()))
    {
        std::fprintf(stderr, "GenerateCubeDemoAssets: could not write '%s'\n",
                     idMapPath.generic_string().c_str());
        return 1;
    }

    std::filesystem::path manifestPath = scenePath;
    manifestPath.replace_extension();
    manifestPath += ".manifest.json";
    if (!WriteAssetManifestFile(manifestPath.generic_string(), manifest))
    {
        std::fprintf(stderr, "GenerateCubeDemoAssets: could not write '%s'\n",
                     manifestPath.generic_string().c_str());
        return 1;
    }

    // The cooked scene: authored refs stamped with their ids. Refs the map
    // does not know stay plain paths, so the cooked output is never less
    // resolvable than the authored input.
    std::filesystem::path cookedScenePath = scenePath;
    cookedScenePath.replace_extension();
    cookedScenePath += ".cooked.json";
    std::ofstream cookedScene(cookedScenePath, std::ios::trunc);
    if (cookedScene.is_open())
        cookedScene << JsonStringify(StampAssetRefIds(*sceneJson, idMap), /*pretty*/ true);
    if (!cookedScene.good())
    {
        std::fprintf(stderr, "GenerateCubeDemoAssets: could not write '%s'\n",
                     cookedScenePath.generic_string().c_str());
        return 1;
    }

    return 0;
}
