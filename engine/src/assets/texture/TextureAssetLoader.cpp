#include <assets/texture/TextureAssetLoader.h>

#include <assets/texture/TextureFormat.h>
#include <assets/texture/TextureLoader.h>
#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/TextureCache.h>
#include <render/Image.h>
#include <render/ImageLoader.h>
#include <render/TextureData.h>

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

    // Sniff the container magic rather than trusting the extension: a cooked
    // artifact keeps its source's virtual path (Decision B), so the path may
    // say ".png" while the bytes are a cooked .stex. The .stex carries its
    // own format and usage tags — `srgb` applies only to loose image bytes.
    if (LooksLikeStex(bytes.data(), bytes.size()))
    {
        TextureData texture;
        std::string stexError;
        if (!LoadStexFromBytes(bytes, texture, &stexError))
        {
            staging.Error = std::format("failed to parse .stex for '{}': {}",
                                        record.Path, stexError);
            return staging;
        }

        staging.Payload = std::move(texture);
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

    if (!Cache)
    {
        Log.Error("TextureAssetLoader: missing TextureCache for '{}'", staged.Record.Path);
        return {};
    }

    TextureHandle handle{};
    if (TextureData* texture = std::any_cast<TextureData>(&staged.Payload))
    {
        handle = Cache->CreateFromTextureData(staged.Record.Path, *texture);
    }
    else if (Image* image = std::any_cast<Image>(&staged.Payload))
    {
        handle = Cache->CreateFromImage(staged.Record.Path, *image);
    }
    else
    {
        Log.Error("TextureAssetLoader: staging payload for '{}' is neither TextureData nor Image",
                  staged.Record.Path);
        return {};
    }

    if (!handle.IsValid())
        Log.Error("TextureAssetLoader: failed to upload texture '{}'", staged.Record.Path);

    return handle;
}

bool TextureAssetLoader::CommitReload(AssetStaging&& staged)
{
    if (!staged.IsValid())
    {
        Log.Error("TextureAssetLoader: reload of failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return false;
    }
    if (!Cache)
    {
        Log.Error("TextureAssetLoader: missing TextureCache for reload of '{}'", staged.Record.Path);
        return false;
    }

    if (TextureData* texture = std::any_cast<TextureData>(&staged.Payload))
        return Cache->ReloadInPlace(staged.Record.Path, *texture);
    if (Image* image = std::any_cast<Image>(&staged.Payload))
        return Cache->ReloadInPlace(staged.Record.Path, *image);

    Log.Error("TextureAssetLoader: reload payload for '{}' is neither TextureData nor Image",
              staged.Record.Path);
    return false;
}
