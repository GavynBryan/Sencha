#pragma once

#include <cstdint>
#include <limits>

#include <math/Vec.h>
#include <physics/PhysicsTypes.h>

//=============================================================================
// Driven pose constraint vocabulary
//
// Backend-free, like PhysicsTypes.h. The backend's whole contract is: drive
// this follower-local frame toward this world-space frame, one-way, while the
// follower collides normally. It never sees the target entity or its local
// attachment frame; frame and velocity composition belong to the ECS-side
// binding. The hidden realization never leaks through these types.
//=============================================================================

struct PhysicsConstraintId
{
    static constexpr uint32_t kInvalid = 0xffffffffu;

    uint32_t Index = kInvalid;
    uint32_t Generation = 0;

    [[nodiscard]] bool IsValid() const { return Index != kInvalid; }
    friend bool operator==(PhysicsConstraintId, PhysicsConstraintId) = default;
};

enum class PoseDriveResponse : uint8_t
{
    Locked,
    Spring,
};

struct LinearPoseDriveSettings
{
    PoseDriveResponse Response = PoseDriveResponse::Locked;
    float FrequencyHz = 0.0f;
    float DampingRatio = 1.0f;
    float MaxForce = std::numeric_limits<float>::infinity();
};

struct AngularPoseDriveSettings
{
    PoseDriveResponse Response = PoseDriveResponse::Locked;
    float FrequencyHz = 0.0f;
    float DampingRatio = 1.0f;
    float MaxTorque = std::numeric_limits<float>::infinity();
};

struct DrivenPoseConstraintDesc
{
    PhysicsBodyId Follower;

    // Attachment frame on the follower, in body space. The backend drives this
    // frame toward the target's world frame.
    BodyTransform FollowerLocalFrame{ Vec3d::Zero(), Quatf::Identity() };

    LinearPoseDriveSettings LinearDrive;
    AngularPoseDriveSettings AngularDrive;

    uint64_t UserData = 0;
};

// Per-step target input. WorldFrame is the target frame at the beginning of the
// upcoming physics step. LinearVelocity and AngularVelocity describe how that
// frame moves through the step, so locked response corrects the current error
// and adds feed-forward motion exactly once.
//
// Velocities are evaluated at the frame origin (v + w x r for off-center
// frames), not at the target object's origin; that composition is the caller's
// job.
struct DrivenPoseTarget
{
    BodyTransform WorldFrame{ Vec3d::Zero(), Quatf::Identity() };
    Vec3d LinearVelocity = Vec3d::Zero();
    Vec3d AngularVelocity = Vec3d::Zero();

    // A discontinuity: the follower snaps to the frame instead of chasing it,
    // so a teleport never manufactures an enormous synthetic velocity.
    bool Teleported = false;
};

// Read after a step, until the constraint is removed. The velocity-drive
// prototype cannot report physical force or torque because it does not expose a
// solver impulse. These diagnostics therefore name exactly what was commanded;
// P3 replaces them with force/torque only when a real solver-side realization
// can aggregate impulses over the outer fixed step.
struct PhysicsConstraintTelemetry
{
    float PositionError = 0.0f;
    float AngularError = 0.0f;
    float CommandedLinearSpeed = 0.0f;
    float CommandedAngularSpeed = 0.0f;
};
