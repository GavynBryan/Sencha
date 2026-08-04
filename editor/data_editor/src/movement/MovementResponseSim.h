#pragma once

#include <gameplay_tags/GameplayTagContainer.h>
#include <math/Vec.h>
#include <movement/MovementComponents.h>
#include <movement/MovementProfileRuntime.h>

#include <functional>
#include <span>
#include <string>
#include <vector>

// Answers the questions a movement tuner actually asks — how high is the jump,
// how long does it hang, how quickly does the character reach top speed — by
// running the profile through the same kernels the game does.
//
// It calls StepFreeLocomotion and ResolveMovementTuning directly rather than
// reimplementing the integration, so a preview cannot quietly disagree with the
// running game. What it does mirror by hand is the tick's phase order (tuning
// resolves before jump execution, so the launch tick is still grounded), and
// MovementResponseSimTests pins that against the real systems.
//
// The world is empty: flat ground at the launch height, no surface velocity, no
// collision beyond the floor. Numbers describe the profile, not a level.

struct MovementSimParams
{
    float Dt = 1.0f / 60.0f;
    float MoveSpeed = 4.5f;
    Vec3d Gravity = Vec3d(0.0f, -9.81f, 0.0f);
    Vec3d UpAxis = Vec3d(0.0f, 1.0f, 0.0f);
    int MaxTicks = 600;
};

struct MovementSimSample
{
    float Time = 0.0f;
    float Height = 0.0f;      // along the up axis, relative to launch
    float Distance = 0.0f;    // planar distance travelled from launch
    float PlanarSpeed = 0.0f;
};

// Builds the resolve context for a phase. Supplied by the caller because tag
// and mode identities live in a registry the simulation does not own.
using MovementSimContextFn = std::function<MovementResolveContext(
    SupportKind support,
    std::span<const std::string> tags,
    GameplayTagContainer& storage)>;

struct MovementJumpArc
{
    std::vector<MovementSimSample> Samples;
    float ApexHeight = 0.0f;
    float TimeToApex = 0.0f;
    float HangTime = 0.0f;     // launch until the character is back at launch height
    float Distance = 0.0f;     // planar distance covered over the hop
    float JumpSpeed = 0.0f;    // the resolved launch speed, for reference
    bool Landed = false;       // false when the arc ran out of ticks
};

struct MovementSpeedResponse
{
    std::vector<MovementSimSample> Samples;
    float TopSpeed = 0.0f;
    float TimeToTopSpeed = 0.0f;   // to within 5% of TopSpeed
    float StoppingDistance = 0.0f; // from top speed once input is released
    bool ReachedTopSpeed = false;
};

// Grounded launch tick, then airborne until the character returns to launch
// height, with full forward input held throughout.
[[nodiscard]] MovementJumpArc SimulateJumpArc(const BoundMovementProfile& profile,
                                              const MovementSimContextFn& makeContext,
                                              const MovementSimParams& params);

// From rest on stable ground with full forward input, then a release to measure
// how far the character slides to a stop.
[[nodiscard]] MovementSpeedResponse SimulateSpeedResponse(
    const BoundMovementProfile& profile,
    const MovementSimContextFn& makeContext,
    const MovementSimParams& params);
