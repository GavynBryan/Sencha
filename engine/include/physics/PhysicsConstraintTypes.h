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
// attachment frame — frame and velocity composition belong to the ECS-side
// binding. The hidden realization never leaks through these types.
//=============================================================================

// Generational handle to a constraint inside a PhysicsWorld. Unlike body ids,
// constraint handles live in ECS link components and outlive breaks in
// telemetry queries, so slot reuse without a generation would be an ABA
// defect. A stale handle never resolves.
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
    Locked, // rigid relative-pose preservation
    Spring, // lags and recovers under FrequencyHz / DampingRatio
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

// Everything needed to create one driven pose constraint. The follower must
// be a dynamic body in the same world.
struct DrivenPoseConstraintDesc
{
    PhysicsBodyId Follower;

    // The attachment frame on the follower, in body space. The backend drives
    // this frame toward the target's world frame.
    BodyTransform FollowerLocalFrame{ Vec3d::Zero(), Quatf::Identity() };

    LinearPoseDriveSettings LinearDrive;
    AngularPoseDriveSettings AngularDrive;

    uint64_t UserData = 0; // opaque tag for the ECS bridge, like BodyDesc::UserData
};

// The per-step target input. A constraint must be refreshed between steps:
// the target is per-step input exactly like a kinematic body's transform.
// Velocities are evaluated at the frame origin (v + w x r for off-center
// frames), not at the target object's origin — that composition is the
// caller's job.
struct DrivenPoseTarget
{
    BodyTransform WorldFrame{ Vec3d::Zero(), Quatf::Identity() };
    Vec3d LinearVelocity = Vec3d::Zero();
    Vec3d AngularVelocity = Vec3d::Zero();

    // A discontinuity: the follower snaps to the frame instead of chasing it,
    // so a teleport never manufactures an enormous synthetic velocity.
    bool Teleported = false;
};

// Read after a step, until the constraint is removed.
struct PhysicsConstraintTelemetry
{
    float PositionError = 0.0f;  // meters, before this step's drive
    float AngularError = 0.0f;   // radians, before this step's drive
    float AppliedForce = 0.0f;   // newtons realized by this step's drive
    float AppliedTorque = 0.0f;  // newton-meters realized by this step's drive
};
