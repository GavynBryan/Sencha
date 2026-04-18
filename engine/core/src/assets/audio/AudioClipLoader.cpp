#include <assets/audio/AudioClipLoader.h>

#include <SDL3/SDL_audio.h>

#include <cstring>
#include <string>

std::optional<AudioClip> LoadAudioClipFromFile(std::string_view path)
{
    const std::string pathStr(path);

    SDL_AudioSpec spec{};
    uint8_t*      rawData  = nullptr;
    uint32_t      rawBytes = 0;

    if (!SDL_LoadWAV(pathStr.c_str(), &spec, &rawData, &rawBytes))
        return std::nullopt;

    // Convert whatever SDL decoded to Sint16 interleaved PCM.
    // SDL_AudioStream handles format conversion so we can push raw data through
    // it and pull Sint16 samples out.
    SDL_AudioSpec srcSpec = spec;
    SDL_AudioSpec dstSpec{};
    dstSpec.format   = SDL_AUDIO_S16;
    dstSpec.channels = spec.channels;
    dstSpec.freq     = spec.freq;

    SDL_AudioStream* conv = SDL_CreateAudioStream(&srcSpec, &dstSpec);
    if (!conv)
    {
        SDL_free(rawData);
        return std::nullopt;
    }

    bool ok = SDL_PutAudioStreamData(conv, rawData, static_cast<int>(rawBytes));
    SDL_free(rawData);

    if (!ok)
    {
        SDL_DestroyAudioStream(conv);
        return std::nullopt;
    }

    SDL_FlushAudioStream(conv);

    const int available = SDL_GetAudioStreamAvailable(conv);
    if (available <= 0)
    {
        SDL_DestroyAudioStream(conv);
        return std::nullopt;
    }

    const uint32_t sampleCount = static_cast<uint32_t>(available) / sizeof(int16_t);

    AudioClip clip;
    clip.SampleRate   = static_cast<uint32_t>(spec.freq);
    clip.ChannelCount = static_cast<uint8_t>(spec.channels);
    clip.Samples.resize(sampleCount);

    const int got = SDL_GetAudioStreamData(conv,
                                           clip.Samples.data(),
                                           available);
    SDL_DestroyAudioStream(conv);

    if (got <= 0)
        return std::nullopt;

    clip.Samples.resize(static_cast<uint32_t>(got) / sizeof(int16_t));
    return clip;
}
