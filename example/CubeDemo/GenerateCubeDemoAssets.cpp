// Build-time dev-asset generator for CubeDemo (docs/assets/pipeline.md,
// Stage 1). Writes the demo's cube mesh as a real .smesh file so the demo
// loads it through the file path like shipped content would. Generating at
// build time (rather than committing the binary) keeps the bytes in sync
// with StaticMeshVertex when the format version moves.
//
// Usage: GenerateCubeDemoAssets <output-assets-root>

#include <assets/static_mesh/StaticMeshSerializer.h>
#include <core/logging/ConsoleLogSink.h>
#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/StaticMeshPrimitives.h>

#include <cstdio>
#include <filesystem>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: GenerateCubeDemoAssets <output-assets-root>\n");
        return 1;
    }

    const std::filesystem::path outRoot{ argv[1] };

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

    return 0;
}
