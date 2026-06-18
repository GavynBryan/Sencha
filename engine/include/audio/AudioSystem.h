#pragma once

#include <audio/AudioVoice.h>

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

class AudioService;
struct AudioContext;
struct Registry;

//=============================================================================
// AudioSystem (docs/audio/runtime.md, Decision D)
//
// The engine-registered system that drives AudioSourceComponent playback. It
// runs in the audio frame lane (FramePhase::Update, presentation time — never
// the fixed tick, so determinism is unaffected) over the active audio
// registries. Dormant preloaded rooms are silent by construction: they are
// simply not in the audio view.
//
// Per frame: tick the service (retire drained voices), sweep voices whose
// registry left the audio view, then visit the active sources and apply the
// start/stop rules (Decision E). The sweep table keys on Registry* but only
// ever copies VoiceIds out of it — a detached registry's pointer is never
// dereferenced (its voices were already stopped by the component's OnRemove,
// and generational ids make a double-stop a no-op).
//=============================================================================
class AudioSystem
{
public:
    // The AudioService is injected at registration, not resolved per frame.
    // Null is the headless-test path (the engine always injects a service);
    // Audio() then no-ops, exactly as an invalid service does.
    explicit AudioSystem(AudioService* audio = nullptr) : AudioBackend(audio) {}

    // Schedule hook: drives the active audio registries through the injected
    // AudioService. An invalid service is a silent no-op (no audio device —
    // CI, headless).
    void Audio(AudioContext& ctx);

    // Engine-free core, for headless tests: tick + sweep + start rules over
    // `active`, using `audio` for playback. Null `audio` is a no-op.
    void Update(AudioService* audio, std::span<Registry*> active);

private:
    void DriveRegistry(AudioService& audio, Registry& registry,
                       std::vector<VoiceId>& playing);

    AudioService* AudioBackend = nullptr;

    // Voices this system started, grouped by the registry whose sources own
    // them. Rebuilt each frame for active registries; entries for registries
    // absent from the new active set get their voices stopped and dropped.
    std::unordered_map<Registry*, std::vector<VoiceId>> PlayingByRegistry;
};
