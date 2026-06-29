#pragma once

#include <memory>

#include <math/Vec.h>

class PhysicsWorld;
struct CharacterMoverImpl;

//=============================================================================
// CharacterMover
//
// Kinematic capsule character on top of Jolt's CharacterVirtual, behind a PIMPL
// so this header stays backend-free and the move-and-slide behavior is testable
// without graphics or Jolt headers. One per character; collides against the
// shared PhysicsWorld. The mover owns vertical velocity (gravity, jump); callers
// supply horizontal intent each tick. Position is the capsule center.
//=============================================================================

struct CharacterMoverConfig
{
    float Radius = 0.3f;
    float Height = 1.8f; // total capsule height; clamped to >= 2 * Radius
    float SlopeLimitDegrees = 50.0f;
    float Mass = 70.0f; // used when pushing dynamic bodies
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

    // Advance one tick: horizontalVelocity is the caller's planar move intent;
    // the mover integrates gravity into its own vertical velocity, then collides
    // and slides against the world.
    void Move(const Vec3d& horizontalVelocity, float dt, const Vec3d& gravity);

    // Set vertical velocity for the next Move (e.g. jump impulse).
    void Jump(float upSpeed);

    [[nodiscard]] bool IsGrounded() const;
    [[nodiscard]] Vec3d GetPosition() const;
    void SetPosition(const Vec3d& position);
    [[nodiscard]] Vec3d GetVelocity() const;

private:
    std::unique_ptr<CharacterMoverImpl> Impl;
};
