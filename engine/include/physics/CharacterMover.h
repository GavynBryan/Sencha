#pragma once

#include <memory>

#include <ecs/EntityId.h>
#include <math/Vec.h>

class PhysicsWorld;
struct CharacterMoverImpl;

//=============================================================================
// CharacterMover
//
// Kinematic capsule character on top of Jolt's CharacterVirtual, behind a
// PIMPL so this header stays backend-free and the move-and-slide behavior is
// testable without graphics or Jolt headers. One per character; collides
// against the shared PhysicsWorld. Position is the capsule center.
//
// The mover collides, slides, applies step and snap policy, and reports what
// it achieved. It owns no velocity: gravity, jumps, and every other
// contribution are composed into the request by the movement pipeline, so a
// character can be driven along any up axis and the same motor serves modes
// that have nothing to do with walking.
//=============================================================================

struct CharacterMoverConfig
{
    float Radius = 0.3f;
    float Height = 1.8f; // total capsule height; clamped to >= 2 * Radius
    float SlopeLimitDegrees = 50.0f;
    float Mass = 70.0f; // used when pushing dynamic bodies

    // Maximum ledge the capsule steps over instead of colliding with.
    float StepHeight = 0.35f;

    // How far below the feet a stable surface is still snapped to, which is
    // what keeps a character on the ground over convex breaks instead of
    // launching. Ignored when the request disables snapping.
    float GroundSnapDistance = 0.25f;

    float SkinWidth = 0.02f;
};

enum class CharacterSupportKind : uint8_t
{
    None,
    Stable, // within SlopeLimitDegrees: standable
    Steep,  // touched, but too steep to stand on
};

// One tick of motion for the capsule, in world space. Velocity is the full
// composed velocity, not a planar intent.
struct CharacterMoveRequest
{
    // The full composed velocity for this tick, gravity already integrated.
    Vec3d Velocity = Vec3d::Zero();
    Vec3d Gravity = Vec3d(0.0f, -9.81f, 0.0f);
    Vec3d UpAxis = Vec3d(0.0f, 1.0f, 0.0f);
    float GravityScale = 1.0f;
    float DeltaSeconds = 0.0f;

    // Cleared by modes that must leave the ground on purpose (a jump's first
    // tick), which snapping would otherwise cancel.
    bool AllowGroundSnap = true;
};

// What the motor found under the capsule after moving.
struct CharacterSupportResult
{
    EntityId Surface;
    Vec3d ContactPoint = Vec3d::Zero();
    Vec3d Normal = Vec3d(0.0f, 1.0f, 0.0f);

    // World velocity of the surface at the contact, so a character on a moving
    // platform can be reasoned about in the platform's frame.
    Vec3d Velocity = Vec3d::Zero();

    CharacterSupportKind Kind = CharacterSupportKind::None;
};

// What actually happened, which is rarely what was asked for: collision and
// sliding change both position and velocity.
struct CharacterMoveResult
{
    Vec3d Position = Vec3d::Zero();
    Vec3d Velocity = Vec3d::Zero();
    CharacterSupportResult Support;
};

class CharacterMover
{
public:
    CharacterMover(PhysicsWorld& world, const CharacterMoverConfig& config, const Vec3d& position);
    ~CharacterMover();

    CharacterMover(CharacterMover&&) noexcept;
    CharacterMover& operator=(CharacterMover&&) noexcept;
    CharacterMover(const CharacterMover&) = delete;
    CharacterMover& operator=(const CharacterMover&) = delete;

    // Advance one tick against the fully composed velocity. Gravity is not
    // integrated here (locomotion already did that); it is passed through for
    // the backend's stair and floor-stick passes, which need its direction.
    [[nodiscard]] CharacterMoveResult Move(const CharacterMoveRequest& request);

    [[nodiscard]] Vec3d GetPosition() const;
    void SetPosition(const Vec3d& position);
    [[nodiscard]] Vec3d GetVelocity() const;

private:
    std::unique_ptr<CharacterMoverImpl> Impl;
};
