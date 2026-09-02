#pragma once

class AudioClipCache;
class AudioService;
class CaptionRuntime;

//=============================================================================
// AudioSourceRuntime
//
// World resource naming the services the audio systems and hooks drive: the
// clip cache AudioSystem and CaptionSystem read clip data from, the service
// that plays voices, and the caption runtime. Any pointer may be null in a
// headless world (no audio device, tests, captions unused). The pointers are
// plain data, safe to store off-thread during an async zone build; only the
// main thread dereferences them.
//
// Not the lifetime seam: a component's clip reference is held through the
// World's AssetStoreTable like every other asset field.
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
