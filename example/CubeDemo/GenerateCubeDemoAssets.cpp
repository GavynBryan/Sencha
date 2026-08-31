// Build-time dev-asset generator for CubeDemo (docs/assets/pipeline.md,
// Stages 1, 3, and 4e). Four jobs, all seeds of the Stage 4 cook step:
//
//   1. Writes the demo's cube mesh as a real .smesh file so the demo loads
//      it through the file path like shipped content would. Generating at
//      build time (rather than committing the binary) keeps the bytes in
//      sync with StaticMeshVertex when the format version moves.
//   2. Derives the scene's dependency table — the transitive closure of every
//      asset:// reference in the scene plus, for each referenced .smat, the
//      texture refs inside it. Derived data, never authored (Decision D).
//   3. Maintains the persisted asset id map (Decision A): every dependency
//      path gets a stable id at first sight; renames keep theirs via the
//      map's content hashes. The map at <assets-root>/asset_ids.json is the
//      committed identity record — this tool only appends and rehashes.
//   4. Emits the cooked scene, <scene-stem>.smap: the authored scene with
//      every known asset ref stamped {"id", "path"}, compiled with its
//      dependency table into the one binary the runtime reads. The authored
//      scene is never modified — it stays the round-trip format.
//
// Usage: GenerateCubeDemoAssets <output-assets-root> <scene-file>

#include <assets/cook/SceneCookOutput.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <core/assets/AssetIdMap.h>
#include <core/json/JsonParser.h>
#include <core/logging/ConsoleLogSink.h>
#include <core/logging/LoggingProvider.h>
#include <assets/static_mesh/StaticMeshPrimitives.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

namespace
{
    constexpr std::string_view kAssetPrefix = "asset://";

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
    std::string parseError;
    std::optional<JsonValue> sceneJson = JsonParseFile(scenePath, &parseError);
    if (!sceneJson)
    {
        std::fprintf(stderr, "GenerateCubeDemoAssets: %s\n", parseError.c_str());
        return 1;
    }

    std::filesystem::path cookedScenePath = scenePath;
    cookedScenePath.replace_extension();
    cookedScenePath += ".smap";

    // The demo scene carries only engine components, so the engine's own
    // serializer set is the complete schema for the compile.
    ComponentSerializerRegistry serializers;
    RegisterEngineSceneSerializers(serializers);

    std::string cookError;
    const bool cooked = WriteCookedScene(
        *sceneJson,
        /*extraRefs*/ {},
        /*collisionCells*/ {},
        serializers,
        [&outRoot](std::string_view assetPath) { return PhysicalPathFor(outRoot, assetPath); },
        outRoot / kAssetIdMapFileName,
        cookedScenePath,
        /*sceneAssetPath*/ {}, &cookError);
    if (!cooked)
    {
        std::fprintf(stderr, "GenerateCubeDemoAssets: %s\n", cookError.c_str());
        return 1;
    }

    return 0;
}
