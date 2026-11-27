#pragma once

#include <audio/AudioVoice.h>
#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <core/assets/AssetRef.h>
#include <ecs/LeaseOwner.h>

#include <string_view>
#include <utility>
#include <vector>

class World;

// The audio operations' provider, over the World's AudioSourceRuntime. Both are
// cosmetic: a presentation runs them, and nothing in the simulation reads them.
class AudioOperations
{
public:
    explicit AudioOperations(World& world)
        : Entities(&world)
    {
    }
    ~AudioOperations();

    AudioOperations(const AudioOperations&) = delete;
    AudioOperations& operator=(const AudioOperations&) = delete;

    SENCHA_VERB("audio.play")
    SENCHA_LABEL("Play sound")
    SENCHA_CATEGORY("Audio")
    SENCHA_DESCRIPTION("Plays a sound once.")
    SENCHA_COSMETIC
    VerbAdmission Play(SENCHA_ARG("clip") SENCHA_LABEL("Clip") SENCHA_DESCRIPTION("The sound to play.") SENCHA_ASSET(Audio)
                           const AssetRef& clip,
                       SENCHA_ARG("gain") SENCHA_LABEL("Gain") SENCHA_DESCRIPTION("Zero is silent, one is as recorded.")
                           SENCHA_RANGE(0, 1) float gain = 1.0f,
                       SENCHA_ARG("bus") SENCHA_LABEL("Bus") SENCHA_DESCRIPTION("The mixer bus it plays on.")
                           std::string_view bus = "Sfx");

    SENCHA_LEASE("audio.loop")
    SENCHA_LABEL("Loop sound")
    SENCHA_CATEGORY("Audio")
    SENCHA_DESCRIPTION("Plays a sound on a loop while held.")
    SENCHA_COSMETIC
    VerbAdmission Loop(const VerbInvocation& invocation,
                       SENCHA_ARG("clip") SENCHA_LABEL("Clip") SENCHA_DESCRIPTION("The sound to play.") SENCHA_ASSET(Audio)
                           const AssetRef& clip,
                       SENCHA_ARG("gain") SENCHA_LABEL("Gain") SENCHA_DESCRIPTION("Zero is silent, one is as recorded.")
                           SENCHA_RANGE(0, 1) float gain = 1.0f,
                       SENCHA_ARG("bus") SENCHA_LABEL("Bus") SENCHA_DESCRIPTION("The mixer bus it plays on.")
                           std::string_view bus = "Sfx");
    SENCHA_LEASE_RELEASE("audio.loop")
    VerbAdmission EndLoop(const VerbInvocation& invocation);

private:
    VoiceId Start(const AssetRef& clip, float gain, std::string_view bus, bool looping, VerbAdmission& admission);
    // Stops every loop whose holder's entity is gone, as the pull rule asks.
    void Sweep();

    World* Entities;
    std::vector<std::pair<LeaseOwner, VoiceId>> Loops;
};

#if !defined(SENCHA_CODEGEN)
#  include <audio/AudioVerbs.sencha.h>
#endif
