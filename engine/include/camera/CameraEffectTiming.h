#pragma once

#include <ecs/EntityId.h>
#include <ecs/LeaseOwner.h>

#include <cstdint>
#include <limits>

class World;

// Seconds to rise to full strength, stay there, and fall back, each on a
// smoothstep ramp. A held effect stays at full strength until let go, then
// releases; Hold applies only to one that is not held.
struct CameraEnvelope
{
    float Attack = 0.0f;
    float Hold = 0.0f;
    float Release = 0.0f;

    friend bool operator==(const CameraEnvelope&, const CameraEnvelope&) = default;
};

[[nodiscard]] bool IsValidCameraEnvelope(const CameraEnvelope& envelope);

// Presentation time on the fixed clock: the last completed tick plus how far
// this frame sits past it. A suspended simulation stops both, so effects pause
// with it, and nothing here depends on the frame rate.
struct CameraEffectClock
{
    std::uint64_t Tick = 0;
    double Alpha = 0.0;
    double TickSeconds = 1.0 / 60.0;
};

// When one presented camera effect started, when it lets go, whose view it
// belongs to, and which lease holds it.
struct CameraEffectTiming
{
    static constexpr double kHeld = std::numeric_limits<double>::infinity();

    CameraEnvelope Envelope;
    // The tick whose presentation started it.
    std::uint64_t StartTick = 0;
    // Seconds after the start at which the release begins.
    double ReleaseAt = kHeld;
    // Only the view this entity owns presents it; none means every view.
    EntityId Subject;
    LeaseOwner Holder;

    [[nodiscard]] static CameraEffectTiming Once(const CameraEnvelope& envelope, std::uint64_t tick, EntityId subject);
    [[nodiscard]] static CameraEffectTiming Held(const CameraEnvelope& envelope, std::uint64_t tick, EntityId subject,
                                                 LeaseOwner holder);

    [[nodiscard]] double Elapsed(const CameraEffectClock& now) const;
    // Lets go as of the end of `tick`; a later release than one already begun is ignored.
    void ReleaseAfter(std::uint64_t tick, double tickSeconds);
    [[nodiscard]] float Level(double elapsed) const;
    [[nodiscard]] bool Expired(double elapsed) const;
};

// The entity whose point of view `camera` presents: its CameraSubject, or the
// camera itself when it names none.
[[nodiscard]] EntityId CameraViewSubject(const World& world, EntityId camera);

// Whether an effect for `subject` belongs in the view `camera` presents.
[[nodiscard]] bool CameraViewIsOwnedBy(const World& world, EntityId camera, EntityId subject);
