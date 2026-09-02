#pragma once

#include <audio/AudioCaptionComponent.h>
#include <audio/AudioSourceComponent.h>
#include <world/ComponentRegistrar.h>

// What a zone sounds like, and what it says while it does.
using AudioComponents = ComponentSet<AudioSourceComponent, AudioCaptionComponent>;

inline void RegisterAudioComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<AudioComponents>();
}
