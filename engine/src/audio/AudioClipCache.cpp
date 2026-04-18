#include <audio/AudioClipCache.h>
#include <audio/AudioClipLoader.h>

AudioClipCache::AudioClipCache(LoggingProvider& logging)
    : Log(logging.GetLogger<AudioClipCache>())
{
    ReserveNullSlot();
}

AudioClipCache::~AudioClipCache()
{
    FreeAllEntries();
}

const AudioClip* AudioClipCache::Get(AudioClipHandle handle) const
{
    const AudioClipEntry* entry = Resolve(handle);
    return entry ? &entry->Clip : nullptr;
}

// -- AssetCache CRTP hooks ---------------------------------------------------

bool AudioClipCache::OnLoad(std::string_view path, AudioClipEntry& out)
{
    auto clip = LoadAudioClipFromFile(path);
    if (!clip)
    {
        Log.Error("AudioClipCache: failed to load '{}'", path);
        return false;
    }
    out.Clip = std::move(*clip);
    return true;
}

void AudioClipCache::OnFree(AudioClipEntry& entry)
{
    entry.Clip = {};
}

bool AudioClipCache::IsEntryLive(const AudioClipEntry& entry) const
{
    return entry.Clip.IsValid();
}
