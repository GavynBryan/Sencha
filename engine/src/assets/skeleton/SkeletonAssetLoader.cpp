#include <assets/skeleton/SkeletonAssetLoader.h>

#include <anim/SkeletonCache.h>
#include <assets/skeleton/SkeletonSerializer.h>
#include <core/logging/LoggingProvider.h>

#include <format>
#include <utility>

SkeletonAssetLoader::SkeletonAssetLoader(LoggingProvider& logging, SkeletonCache* cache)
    : Log(logging.GetLogger<SkeletonAssetLoader>())
    , Cache(cache)
{
}

AssetStaging SkeletonAssetLoader::LoadStaged(const AssetRecord& record, IAssetSource& source)
{
    AssetStaging staging;
    staging.Record = record;

    std::vector<std::byte> bytes;
    if (!ReadAssetBytes(source, record, bytes))
    {
        staging.Error = std::format("could not read skeleton source for '{}'", record.Path);
        return staging;
    }

    SkeletonData data;
    std::string error;
    if (!LoadSskelFromBytes(bytes, data, &error))
    {
        staging.Error = std::format("failed to parse .sskel for '{}': {}", record.Path, error);
        return staging;
    }

    staging.Payload = std::move(data);
    return staging;
}

AssetCommitResult SkeletonAssetLoader::Commit(AssetStaging&& staged)
{
    return { CommitTyped(std::move(staged)).IsValid() };
}

SkeletonHandle SkeletonAssetLoader::CommitTyped(AssetStaging&& staged)
{
    if (!staged.IsValid())
    {
        Log.Error("SkeletonAssetLoader: commit of failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return {};
    }

    SkeletonData* data = std::any_cast<SkeletonData>(&staged.Payload);
    if (data == nullptr)
    {
        Log.Error("SkeletonAssetLoader: staging payload for '{}' is not SkeletonData",
                  staged.Record.Path);
        return {};
    }

    if (!Cache)
    {
        Log.Error("SkeletonAssetLoader: missing SkeletonCache for '{}'", staged.Record.Path);
        return {};
    }

    SkeletonHandle handle = Cache->Register(staged.Record.Path, std::move(*data));
    if (!handle.IsValid())
        Log.Error("SkeletonAssetLoader: failed to register skeleton '{}'", staged.Record.Path);

    return handle;
}
