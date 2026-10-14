#pragma once

#include <audio/AudioVoice.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <ecs/LeaseOwner.h>

#include <string_view>
#include <utility>
#include <vector>

class World;

// Play a clip once. Non-positional until audio has a listener.
inline constexpr std::string_view kAudioPlayVerb = "audio.play";
// Play a clip on a loop while held.
inline constexpr std::string_view kAudioLoopLease = "audio.loop";

void DeclareAudioVerbs(VerbRegistrationScope& scope);

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

    VerbAdmission Play(const VerbInvocation& invocation);
    VerbAdmission Loop(const VerbInvocation& invocation);

private:
    VoiceId Start(const VerbInvocation& invocation, bool looping, VerbAdmission& admission);
    // Stops every loop whose holder's entity is gone, as the pull rule asks.
    void Sweep();

    World* Entities;
    std::vector<std::pair<LeaseOwner, VoiceId>> Loops;
};

inline VerbAdmission InvokeAudioPlay(AudioOperations& operations, const VerbInvocation& invocation)
{
    return operations.Play(invocation);
}

inline VerbAdmission InvokeAudioLoop(AudioOperations& operations, const VerbInvocation& invocation)
{
    return operations.Loop(invocation);
}
