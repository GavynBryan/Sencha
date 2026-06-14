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
    // Schedule hook: resolves AudioService from the engine and drives the
    // active audio registries. A missing or invalid service is a silent
    // no-op (no audio device — CI, headless).
    void Audio(AudioContext& ctx);

    // Engine-free core, for headless tests: tick + sweep + start rules over
    // `active`, using `audio` for playback. Null `audio` is a no-op.
    void Update(AudioService* audio, std::span<Registry*> active);

private:
    void DriveRegistry(AudioService& audio, Registry& registry,
                       std::vector<VoiceId>& playing);

    // Voices this system started, grouped by the registry whose sources own
    // them. Rebuilt each frame for active registries; entries for registries
    // absent from the new active set get their voices stopped and dropped.
    std::unordered_map<Registry*, std::vector<VoiceId>> PlayingByRegistry;
};
