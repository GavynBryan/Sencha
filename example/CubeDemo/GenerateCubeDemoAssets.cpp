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

#include <assets/cook/SceneCookOutput.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <core/assets/AssetIdMap.h>
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

    MeshSerializer serializer(logging);
    const std::string meshPath = (outRoot / "meshes/dev/cube.smesh").generic_string();
    if (!serializer.WriteToFile(meshPath, StaticMeshPrimitives::BuildCube(1.0f)))
        return 1;

    // Manifest, id map, and stamped cooked scene: the shared cook-scene output
    // (one level of .smat indirection, stable ids, id-stamped scene). The demo's
    // asset:// mapping is the flat root/x; it has no Generated refs of its own.
    std::optional<JsonValue> sceneJson = ParseJsonFile(scenePath);
    if (!sceneJson)
        return 1;

    std::filesystem::path manifestPath = scenePath;
    manifestPath.replace_extension();
    manifestPath += ".manifest.json";

    std::filesystem::path cookedScenePath = scenePath;
    cookedScenePath.replace_extension();
    cookedScenePath += ".cooked.json";

    std::string cookError;
    const bool cooked = WriteCookedScene(
        *sceneJson,
        /*extraRefs*/ {},
        [&outRoot](std::string_view assetPath) { return PhysicalPathFor(outRoot, assetPath); },
        outRoot / kAssetIdMapFileName,
        manifestPath,
        cookedScenePath,
        &cookError);
    if (!cooked)
    {
        std::fprintf(stderr, "GenerateCubeDemoAssets: %s\n", cookError.c_str());
        return 1;
    }

    return 0;
}
