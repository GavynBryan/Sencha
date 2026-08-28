#include <assets/scene/SceneAssetLoader.h>

#include <core/logging/LoggingProvider.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <cstddef>
#include <format>
#include <utility>
#include <vector>

SceneAssetLoader::SceneAssetLoader(LoggingProvider& logging,
                                   SceneCache* cache,
                                   const ComponentSerializerRegistry* serializers)
    : Log(logging.GetLogger<SceneAssetLoader>())
    , Cache(cache)
    , Serializers(serializers)
{
}

AssetStaging SceneAssetLoader::LoadStaged(const AssetRecord& record, IAssetSource& source)
{
    AssetStaging staging;
    staging.Record = record;

    if (Serializers == nullptr)
    {
        staging.Error = std::format(
            "no component serializer registry is wired, so scene '{}' cannot "
            "be schema-checked", record.Path);
        return staging;
    }

    std::vector<std::byte> bytes;
    if (!ReadAssetBytes(source, record, bytes))
    {
        staging.Error = std::format("could not read scene source for '{}'", record.Path);
        return staging;
    }

    SmapContents contents;
    SmapError error;
    if (!ReadSmap(bytes, *Serializers, contents, &error))
    {
        staging.Error = std::format("failed to parse .smap for '{}': {}",
                                    record.Path, error.Message);
        return staging;
    }

    staging.Payload = std::move(contents);
    return staging;
}

SceneHandle SceneAssetLoader::CommitTyped(AssetStaging&& staged)
{
    if (!staged.IsValid())
    {
        Log.Error("SceneAssetLoader: commit of failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return {};
    }

    if (Cache == nullptr)
    {
        Log.Error("SceneAssetLoader: missing SceneCache for '{}'", staged.Record.Path);
        return {};
    }

    SmapContents* contents = std::any_cast<SmapContents>(&staged.Payload);
    if (contents == nullptr)
    {
        Log.Error("SceneAssetLoader: staging payload for '{}' is not SmapContents",
                  staged.Record.Path);
        return {};
    }

    SceneHandle handle = Cache->Register(staged.Record.Path, std::move(*contents));
    if (!handle.IsValid())
        Log.Error("SceneAssetLoader: failed to register scene '{}'", staged.Record.Path);

    return handle;
}
