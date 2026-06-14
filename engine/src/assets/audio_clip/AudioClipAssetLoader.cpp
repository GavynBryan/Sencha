#include <assets/audio_clip/AudioClipAssetLoader.h>

#include <assets/audio_clip/AudioClipFormat.h>
#include <assets/audio_clip/AudioClipSerializer.h>
#include <audio/AudioClipLoader.h>
#include <core/logging/LoggingProvider.h>

#include <format>
#include <optional>
#include <utility>

AudioClipAssetLoader::AudioClipAssetLoader(LoggingProvider& logging, AudioClipCache* cache)
    : Log(logging.GetLogger<AudioClipAssetLoader>())
    , Cache(cache)
{
}

AssetStaging AudioClipAssetLoader::LoadStaged(const AssetRecord& record, IAssetSource& source)
{
    AssetStaging staging;
    staging.Record = record;

    std::vector<std::byte> bytes;
    if (!ReadAssetBytes(source, record, bytes))
    {
        staging.Error = std::format("could not read audio source for '{}'", record.Path);
        return staging;
    }

    // Sniff the container magic rather than trusting the extension: a cooked
    // artifact keeps its source's virtual path (Decision B), so the path may
    // say ".wav" while the bytes are a cooked .sclip.
    if (LooksLikeSclip(bytes.data(), bytes.size()))
    {
        AudioClip clip;
        std::string sclipError;
        if (!LoadSclipFromBytes(bytes, clip, &sclipError))
        {
            staging.Error = std::format("failed to parse .sclip for '{}': {}",
                                        record.Path, sclipError);
            return staging;
        }

        staging.Payload = std::move(clip);
        return staging;
    }

    std::optional<AudioClip> clip = LoadAudioClipFromWavBytes(bytes);
    if (!clip)
    {
        staging.Error = std::format("failed to decode audio data for '{}'", record.Path);
        return staging;
    }

    staging.Payload = std::move(*clip);
    return staging;
}

AssetCommitResult AudioClipAssetLoader::Commit(AssetStaging&& staged)
{
    return { CommitTyped(std::move(staged)).IsValid() };
}

AudioClipHandle AudioClipAssetLoader::CommitTyped(AssetStaging&& staged)
{
    if (!staged.IsValid())
    {
        Log.Error("AudioClipAssetLoader: commit of failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return {};
    }

    if (!Cache)
    {
        Log.Error("AudioClipAssetLoader: missing AudioClipCache for '{}'", staged.Record.Path);
        return {};
    }

    AudioClip* clip = std::any_cast<AudioClip>(&staged.Payload);
    if (!clip)
    {
        Log.Error("AudioClipAssetLoader: staging payload for '{}' is not an AudioClip",
                  staged.Record.Path);
        return {};
    }

    AudioClipHandle handle = Cache->Register(staged.Record.Path, std::move(*clip));
    if (!handle.IsValid())
        Log.Error("AudioClipAssetLoader: failed to register audio clip '{}'", staged.Record.Path);

    return handle;
}
