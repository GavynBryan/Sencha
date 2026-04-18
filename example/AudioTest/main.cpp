#include <audio/AudioClipCache.h>
#include <audio/AudioService.h>
#include <core/logging/ConsoleLogSink.h>
#include <core/logging/LoggingProvider.h>
#include <platform/SdlVideoService.h>
#include <platform/SdlWindowService.h>
#include <platform/WindowCreateInfo.h>

#include <SDL3/SDL.h>

#include <cstdio>

static constexpr const char* kSoundPath  = "sampleSound.wav";
static constexpr const char* kBusName    = "Sfx";
static constexpr float       kVolumeStep = 0.1f;
static constexpr float       kVolumeMin  = 0.0f;
static constexpr float       kVolumeMax  = 1.0f;

int main()
{
    // -- Logging -------------------------------------------------------
    LoggingProvider logging;
    logging.AddSink<ConsoleLogSink>();

    // -- Window (required for keyboard events to be delivered by the OS) --
    SdlVideoService  video(logging);
    SdlWindowService windows(logging, video);

    WindowCreateInfo windowInfo;
    windowInfo.Title      = "Audio Test";
    windowInfo.Width      = 480;
    windowInfo.Height     = 120;
    windowInfo.GraphicsApi = WindowGraphicsApi::None;
    windows.CreateWindow(windowInfo);

    // -- Audio services ------------------------------------------------
    EngineAudioConfig audioConfig;
    {
        EngineAudioBusConfig sfx;
        sfx.Name        = kBusName;
        sfx.MaxVoices   = 1;
        sfx.Volume      = 1.0f;
        sfx.StealPolicy = VoiceStealPolicy::StealOldest;
        audioConfig.Buses.push_back(std::move(sfx));
    }

    AudioClipCache clipCache(logging);
    AudioService   audio(logging, audioConfig);

    if (!audio.IsValid())
    {
        std::fprintf(stderr, "AudioService failed to initialize.\n");
        return 1;
    }

    // -- Load clip -----------------------------------------------------
    AudioClipHandle clip = clipCache.Acquire(kSoundPath);
    if (!clip.IsValid())
    {
        std::fprintf(stderr, "Failed to load '%s'.\n", kSoundPath);
        return 1;
    }

    std::printf("Audio test running.\n");
    std::printf("  Space      — play sound\n");
    std::printf("  Up arrow   — volume up\n");
    std::printf("  Down arrow — volume down\n");
    std::printf("  Escape     — quit\n\n");

    // -- Event loop ----------------------------------------------------
    const SdlWindowService::WindowId primaryId = windows.GetPrimaryWindowId();
    bool running = true;

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            windows.HandleEvent(event);

            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
                break;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
            {
                switch (event.key.scancode)
                {
                case SDL_SCANCODE_ESCAPE:
                    running = false;
                    break;

                case SDL_SCANCODE_SPACE:
                {
                    const AudioClip* pcm = clipCache.Get(clip);
                    if (pcm)
                    {
                        PlayParams params;
                        params.Bus = kBusName;
                        [[maybe_unused]] VoiceId voice = audio.Play(AssetId{clip.Id}, *pcm, params);
                    }
                    break;
                }

                case SDL_SCANCODE_UP:
                {
                    float vol = audio.GetBusVolume(kBusName);
                    vol = SDL_min(kVolumeMax, vol + kVolumeStep);
                    audio.SetBusVolume(kBusName, vol);
                    std::printf("Volume: %.0f%%\n", vol * 100.0f);
                    break;
                }

                case SDL_SCANCODE_DOWN:
                {
                    float vol = audio.GetBusVolume(kBusName);
                    vol = SDL_max(kVolumeMin, vol - kVolumeStep);
                    audio.SetBusVolume(kBusName, vol);
                    std::printf("Volume: %.0f%%\n", vol * 100.0f);
                    break;
                }

                default:
                    break;
                }
            }
        }

        if (windows.IsCloseRequested(primaryId))
            running = false;

        audio.Tick();
        SDL_Delay(16);
    }

    clipCache.Release(clip);
    return 0;
}
