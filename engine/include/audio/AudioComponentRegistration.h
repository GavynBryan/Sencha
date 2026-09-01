#pragma once

#include <audio/AudioCaptionComponent.h>
#include <audio/AudioComponentSchemas.h>
#include <audio/AudioSourceComponent.h>
#include <world/ComponentRegistrar.h>

// What a zone sounds like, and what it says while it does.
inline void RegisterAudioComponents(ComponentRegistrar& registrar)
{
    registrar.Add<AudioSourceComponent>();
    registrar.Add<AudioCaptionComponent>();
}
