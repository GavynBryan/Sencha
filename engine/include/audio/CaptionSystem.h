#pragma once

#include <span>

class AudioService;
class CaptionRuntime;
struct AudioContext;
struct Registry;

//=============================================================================
// CaptionSystem (docs/audio/captions-and-dialogue.md, Decision E)
//
// The engine-registered system that begins captions for scene audio sources
// carrying an AudioCaptionComponent, then ticks CaptionRuntime. It runs in
// the audio frame lane, registered after AudioSystem so that voices started
// or swept this frame are observed the same frame.
//
// The matching is state-reactive, no events: a sibling source whose Voice is
// valid and differs from the component's CaptionedVoice gets a voice-bound
// caption (covers first start and loop restart after zone re-entry; a
// one-shot's latched stale voice matches and never re-captions). A source
// that attempted playback and never got a voice takes the Decision C degrade
// path exactly once. AudioSystem never learns about captions, and this
// system never starts or stops voices.
//
// Caption retirement needs no sweep table: voice-bound captions of stopped,
// stolen, or swept voices fall out inside CaptionRuntime::Tick via voice
// state, and component removal (entity destruction, zone detach) ends its
// caption through the component's OnRemove hook.
//=============================================================================
class CaptionSystem
{
public:
    // CaptionRuntime and AudioService are injected at registration, not
    // resolved per frame. Null is the headless-test path (the engine always
    // injects); Update's null handling covers it.
    CaptionSystem(CaptionRuntime* captions = nullptr, AudioService* audio = nullptr)
        : Captions(captions), AudioBackend(audio) {}

    // Schedule hook: drives the active audio registries through the injected
    // CaptionRuntime and AudioService. An invalid audio service takes the
    // no-device degrade path.
    void Audio(AudioContext& ctx);

    // Engine-free core, for headless tests. Null `captions` is a no-op.
    // Null `audio` means "no usable device": active PlayOnActive sources
    // count as failed playback (their subtitles degrade to timed captions —
    // exactly when a player needs text most), and voice-bound captions
    // retire only by duration or explicit end.
    void Update(CaptionRuntime* captions, const AudioService* audio,
                std::span<Registry*> active, float dtSeconds);

private:
    void DriveRegistry(CaptionRuntime& captions, Registry& registry, bool noAudio);

    CaptionRuntime* Captions = nullptr;
    AudioService* AudioBackend = nullptr;
};
