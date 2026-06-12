#pragma once

#include <audio/AudioClip.h>
#include <core/assets/AssetCache.h>
#include <core/logging/LoggingProvider.h>
#include <core/handle/LifetimeHandle.h>

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
// Path-keyed, ref-counted cache for AudioClips. The cache performs no file
// IO (docs/assets/pipeline.md, Decision I — loaders receive bytes): decoded
// clips enter through Register(), which AudioClipAssetLoader's commit half
// and the procedural paths share. Acquire() only ever resolves entries that
// are already registered.
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

    // Registers a decoded clip under `path` (refcount 1, owned by the
    // caller). If `path` is already registered, the existing entry gains a
    // reference and `clip` is discarded — first registration wins, the
    // dedup contract every cache shares.
    [[nodiscard]] AudioClipHandle Register(std::string_view path, AudioClip clip);

    // Resolves a registered path without taking a reference. Invalid handle
    // if the path is unknown.
    [[nodiscard]] AudioClipHandle Find(std::string_view path) const;

    [[nodiscard]] std::string_view GetName(AudioClipHandle handle) const;

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
