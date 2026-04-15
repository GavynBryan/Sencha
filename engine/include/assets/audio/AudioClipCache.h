#pragma once

#include <assets/audio/AudioClip.h>
#include <core/assets/AssetCache.h>
#include <core/logging/LoggingProvider.h>
#include <core/raii/LifetimeHandle.h>

#include <cstdint>
#include <string>

//=============================================================================
// AudioClipHandle
//
// Opaque generational handle returned by AudioClipCache.
//=============================================================================
struct AudioClipHandle
{
    uint32_t Id = 0;

    [[nodiscard]] bool IsValid() const { return Id != 0; }
    bool operator==(const AudioClipHandle&) const = default;
};

//=============================================================================
// AudioClipEntry
//
// Internal slot type used by AssetCache. Lives outside AudioClipCache so it
// can be named in the base class template argument without a forward reference.
//=============================================================================
struct AudioClipEntry
{
    AudioClip   Clip;
    uint32_t    Generation = 0;
    uint32_t    RefCount   = 0;
    std::string PathKey;
};

class AudioClipCache;
using AudioClipCacheHandle = LifetimeHandle<AudioClipCache, AudioClipHandle>;

//=============================================================================
// AudioClipCache
//
// Path-keyed, ref-counted cache for AudioClips. Loads WAV files via
// LoadAudioClipFromFile, deduplicates by path, and returns stable handles.
//
// Acquire() increments the refcount; Release() decrements it and frees the
// PCM data when it reaches zero. AcquireOwned() wraps the handle in an
// AudioClipCacheHandle that calls Release() automatically on destruction.
//=============================================================================
class AudioClipCache : public AssetCache<AudioClipCache, AudioClipHandle, AudioClipEntry>
{
public:
    explicit AudioClipCache(LoggingProvider& logging);
    ~AudioClipCache() override;

    AudioClipCache(const AudioClipCache&) = delete;
    AudioClipCache& operator=(const AudioClipCache&) = delete;
    AudioClipCache(AudioClipCache&&) = delete;
    AudioClipCache& operator=(AudioClipCache&&) = delete;

    // Returns nullptr if the handle is invalid or has been released.
    [[nodiscard]] const AudioClip* Get(AudioClipHandle handle) const;

private:
    friend class AssetCache<AudioClipCache, AudioClipHandle, AudioClipEntry>;

    // AssetCache CRTP hooks.
    bool OnLoad(std::string_view path, AudioClipEntry& out);
    void OnFree(AudioClipEntry& entry);
    bool IsEntryLive(const AudioClipEntry& entry) const;

    Logger& Log;
};
