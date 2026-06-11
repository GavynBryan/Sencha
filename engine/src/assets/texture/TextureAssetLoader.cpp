#include <assets/texture/TextureAssetLoader.h>

#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/TextureCache.h>
#include <render/Image.h>
#include <render/ImageLoader.h>

#include <format>
#include <optional>
#include <utility>

TextureAssetLoader::TextureAssetLoader(LoggingProvider& logging, TextureCache* cache)
    : Log(logging.GetLogger<TextureAssetLoader>())
    , Cache(cache)
{
}

AssetStaging TextureAssetLoader::LoadStaged(const AssetRecord& record, IAssetSource& source)
{
    return LoadStaged(record, source, /*srgb*/ true);
}

AssetStaging TextureAssetLoader::LoadStaged(const AssetRecord& record,
                                            IAssetSource& source,
                                            bool srgb)
{
    AssetStaging staging;
    staging.Record = record;

    std::vector<std::byte> bytes;
    if (!ReadAssetBytes(source, record, bytes))
    {
        staging.Error = std::format("could not read texture source for '{}'", record.Path);
        return staging;
    }

    std::optional<Image> image = LoadImageFromMemory(
        reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<int>(bytes.size()), srgb);
    if (!image)
    {
        staging.Error = std::format("failed to decode image data for '{}'", record.Path);
        return staging;
    }

    staging.Payload = std::move(*image);
    return staging;
}

AssetCommitResult TextureAssetLoader::Commit(AssetStaging&& staged)
{
    return { CommitTyped(std::move(staged)).IsValid() };
}

TextureHandle TextureAssetLoader::CommitTyped(AssetStaging&& staged)
{
    if (!staged.IsValid())
    {
        Log.Error("TextureAssetLoader: commit of failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return {};
    }

    Image* image = std::any_cast<Image>(&staged.Payload);
    if (image == nullptr)
    {
        Log.Error("TextureAssetLoader: staging payload for '{}' is not an Image",
                  staged.Record.Path);
        return {};
    }

    if (!Cache)
    {
        Log.Error("TextureAssetLoader: missing TextureCache for '{}'", staged.Record.Path);
        return {};
    }

    TextureHandle handle = Cache->CreateFromImage(staged.Record.Path, *image);
    if (!handle.IsValid())
        Log.Error("TextureAssetLoader: failed to upload texture '{}'", staged.Record.Path);

    return handle;
}
