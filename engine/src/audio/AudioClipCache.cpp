#include <audio/AudioClipCache.h>

#include <utility>

AudioClipCache::AudioClipCache(LoggingProvider& logging)
    : Log(logging.GetLogger<AudioClipCache>())
{
    ReserveNullSlot();
}

AudioClipCache::~AudioClipCache()
{
    FreeAllEntries();
}

AudioClipHandle AudioClipCache::Register(std::string_view path, AudioClip clip)
{
    if (!clip.IsValid())
    {
        Log.Error("AudioClipCache: refusing to register invalid clip '{}'", path);
        return {};
    }

    AudioClipEntry entry{};
    entry.Clip = std::move(clip);
    return AllocNamedHandle(path, std::move(entry));
}

AudioClipHandle AudioClipCache::Find(std::string_view path) const
{
    return FindRegisteredHandle(path);
}

std::string_view AudioClipCache::GetName(AudioClipHandle handle) const
{
    return GetRegisteredPath(handle);
}

const AudioClip* AudioClipCache::Get(AudioClipHandle handle) const
{
    const AudioClipEntry* entry = Resolve(handle);
    return entry ? &entry->Clip : nullptr;
}

// -- AssetCache CRTP hooks ---------------------------------------------------

bool AudioClipCache::OnLoad(std::string_view, AudioClipEntry&)
{
    // No file IO in the cache (Decision I): Acquire resolves registered
    // entries only; decode lives in AudioClipAssetLoader's stage half.
    return false;
}

void AudioClipCache::OnFree(AudioClipEntry& entry)
{
    entry.Clip = {};
}

bool AudioClipCache::IsEntryLive(const AudioClipEntry& entry) const
{
    return entry.Clip.IsValid();
}
