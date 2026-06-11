// Build-time dev-asset generator for CubeDemo (docs/assets/pipeline.md,
// Stages 1 and 3). Two jobs, both seeds of the Stage 4 cook step:
//
//   1. Writes the demo's cube mesh as a real .smesh file so the demo loads
//      it through the file path like shipped content would. Generating at
//      build time (rather than committing the binary) keeps the bytes in
//      sync with StaticMeshVertex when the format version moves.
//   2. Derives the scene's asset manifest — the transitive closure of every
//      asset:// reference in the scene plus, for each referenced .smat, the
//      texture refs inside it. Derived data, never authored (Decision D).
//
// Usage: GenerateCubeDemoAssets <output-assets-root> <scene-file>
//   The manifest is written next to the scene file as
//   <scene-stem>.manifest.json.

#include <assets/static_mesh/StaticMeshSerializer.h>
#include <core/assets/AssetManifest.h>
#include <core/json/JsonParser.h>
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

    AssetManifest manifest;
    manifest.Paths = CollectAssetPaths(*sceneJson);

    std::unordered_set<std::string> seen(manifest.Paths.begin(), manifest.Paths.end());
    const std::size_t sceneRefCount = manifest.Paths.size();
    for (std::size_t i = 0; i < sceneRefCount; ++i)
    {
        const std::string& path = manifest.Paths[i];
        if (!path.ends_with(".smat"))
            continue;

        std::optional<JsonValue> smatJson = ParseJsonFile(PhysicalPathFor(outRoot, path));
        if (!smatJson)
            return 1;

        for (std::string& ref : CollectAssetPaths(*smatJson))
        {
            if (seen.insert(ref).second)
                manifest.Paths.push_back(std::move(ref));
        }
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

    return 0;
}
