#include <assets/static_mesh/StaticMeshAssetLoader.h>

#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/MeshGeometry.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <format>
#include <utility>

StaticMeshAssetLoader::StaticMeshAssetLoader(LoggingProvider& logging, StaticMeshCache* cache)
    : Log(logging.GetLogger<StaticMeshAssetLoader>())
    , Cache(cache)
    , FileLoader(logging)
{
}

AssetStaging StaticMeshAssetLoader::LoadStaged(const AssetRecord& record, IAssetSource& source)
{
    AssetStaging staging;
    staging.Record = record;

    std::vector<std::byte> bytes;
    if (!ReadAssetBytes(source, record, bytes))
    {
        staging.Error = std::format("could not read static mesh source for '{}'", record.Path);
        return staging;
    }

    MeshGeometry data;
    if (!FileLoader.LoadFromBytes(bytes, data))
    {
        staging.Error = std::format("failed to parse .smesh data for '{}'", record.Path);
        return staging;
    }

    staging.Payload = std::move(data);
    return staging;
}

AssetCommitResult StaticMeshAssetLoader::Commit(AssetStaging&& staged)
{
    return { CommitTyped(std::move(staged)).IsValid() };
}

StaticMeshHandle StaticMeshAssetLoader::CommitTyped(AssetStaging&& staged)
{
    if (!staged.IsValid())
    {
        Log.Error("StaticMeshAssetLoader: commit of failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return {};
    }

    MeshGeometry* data = std::any_cast<MeshGeometry>(&staged.Payload);
    if (data == nullptr)
    {
        Log.Error("StaticMeshAssetLoader: staging payload for '{}' is not MeshGeometry",
                  staged.Record.Path);
        return {};
    }

    if (!Cache)
    {
        Log.Error("StaticMeshAssetLoader: missing StaticMeshCache for '{}'", staged.Record.Path);
        return {};
    }

    StaticMeshHandle handle = Cache->CreateFromData(staged.Record.Path, *data);
    if (!handle.IsValid())
        Log.Error("StaticMeshAssetLoader: failed to upload static mesh '{}'", staged.Record.Path);

    return handle;
}
