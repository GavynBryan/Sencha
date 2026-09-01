#pragma once

class AudioClipCache;
class AudioService;
class CaptionRuntime;

//=============================================================================
// AudioSourceRuntime
//
// World resource the audio component hooks reach through —
// AudioSourceComponent for clips/voices and AudioCaptionComponent for
// captions. Any pointer may be null in headless worlds (no audio device,
// tests, captions unused). The pointers are plain data, safe to store
// off-thread during an async zone build; only the main thread dereferences
// them.
//=============================================================================
struct AudioSourceRuntime
{
    AudioSourceRuntime() = default;
    AudioSourceRuntime(AudioClipCache* clips, AudioService* audio,
                       CaptionRuntime* captions = nullptr)
        : Clips(clips)
        , Audio(audio)
        , Captions(captions)
    {
    }

    AudioClipCache* Clips = nullptr;
    AudioService* Audio = nullptr;
    CaptionRuntime* Captions = nullptr;
};
