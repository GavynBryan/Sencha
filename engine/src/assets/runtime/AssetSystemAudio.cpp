#include <assets/runtime/AssetSystem.h>

#include <core/logging/LoggingProvider.h>

#include <audio/AudioClipCache.h>

#include <utility>

// Audio clip loads.

std::string_view AssetSystem::GetPathForAudioClip(AudioClipHandle handle) const
{
    return AudioClips ? AudioClips->GetName(handle) : std::string_view{};
}

AudioClipHandle AssetSystem::LoadAudioClip(std::string_view path)
{
    const AssetRecord* record = Resolve(path, AssetType::Audio);
    if (!record)
        return {};

    if (!AudioClips)
    {
        Log.Error("AssetSystem: missing AudioClipCache for audio asset {}", record->Path);
        return {};
    }

    switch (record->SourceKind)
    {
    case AssetSourceKind::Procedural:
    {
        AudioClipHandle handle = AudioClips->Acquire(record->Path);
        if (!handle.IsValid())
        {
            Log.Error("AssetSystem: audio clip cache has no runtime resource for path {}", record->Path);
            return {};
        }

        return handle;
    }
    case AssetSourceKind::File:
    {
        if (AudioClipHandle existing = AudioClips->Acquire(record->Path); existing.IsValid())
            return existing;

        AssetStaging staging = ClipLoader.LoadStaged(*record, Source);
        if (!staging.IsValid())
        {
            Log.Error("AssetSystem: {}", staging.Error);
            return {};
        }

        return ClipLoader.CommitTyped(std::move(staging));
    }
    default:
        Log.Error("AssetSystem: unknown audio clip source kind for path {}", record->Path);
        return {};
    }
}

AudioClipHandle AssetSystem::TryAcquireAudioClip(std::string_view path)
{
    return AudioClips ? AudioClips->Acquire(path) : AudioClipHandle{};
}

void AssetSystem::ReleaseAudioClip(AudioClipHandle handle)
{
    if (AudioClips)
        AudioClips->Release(handle);
}
