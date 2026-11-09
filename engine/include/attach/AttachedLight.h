#pragma once

#include <attach/Attachment.h>
#include <camera/CameraEffectTiming.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class FixedSimulationLoop;

// The shortest a rise or fade may be under reduced flashing, in seconds.
inline constexpr float kReducedFlashingRampSeconds = 0.25f;

struct AttachedLightSettings
{
    // No peak above the steady brightness, and no sudden rise or fade.
    bool ReducedFlashing = false;
};

struct AttachedLightSpec
{
    LightAttachmentSpec Attachment;
    CameraEnvelope Envelope;
    // Starts at this multiple of the light's intensity, settling back with a
    // time constant of PulseSeconds.
    float PeakScale = 1.0f;
    float PulseSeconds = 0.0f;
};

// A World resource, local to this machine and never replicated: the lights
// bodies wear for as long as a holder asks, and their fades after. The
// attachment's own light holds the presented brightness; the authored one is
// kept here.
class AttachedLightState
{
public:
    AttachedLightSettings Settings;

    // Holding again for the same holder changes the light in place.
    AttachResult Hold(World& world, AttachedLightSpec spec, std::uint64_t tick, const LeaseOwner& holder);
    void Release(const LeaseOwner& holder, std::uint64_t tick, double tickSeconds);
    void Update(World& world, const CameraEffectClock& now);

    [[nodiscard]] std::size_t Count() const { return Lights.size(); }

private:
    struct Held
    {
        EntityId Entity;
        CameraEffectTiming Timing;
        float Intensity = 0.0f;
        float PeakScale = 1.0f;
        float PulseSeconds = 0.0f;
    };

    std::vector<Held> Lights;
};

// Advances the World's worn lights to this frame's presentation time.
void PresentAttachedLights(World& world, const FixedSimulationLoop& clock, double presentationAlpha);
