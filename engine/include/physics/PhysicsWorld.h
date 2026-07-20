#pragma once

#include <cstdint>
#include <memory>

#include <math/Quat.h>
#include <math/Vec.h>
#include <physics/CollisionShape.h>
#include <physics/PhysicsConstraintTypes.h>
#include <physics/PhysicsTypes.h>

//=============================================================================
// PhysicsWorld
//=============================================================================

struct PhysicsWorldImpl;
class CollisionShapeCache;

struct PhysicsWorldConfig
{
    Vec3d Gravity = Vec3d(0.0f, -9.81f, 0.0f);

    uint32_t MaxBodies = 10240;
    uint32_t MaxBodyPairs = 16384;
    uint32_t MaxContactConstraints = 8192;

    // Ceiling on the error-closing speed a driven pose constraint commands,
    // beyond the target frame's own velocity. Discrete collision cannot resolve
    // arbitrary closing speeds; the cap keeps large initial error from becoming
    // an immediate tunneling request.
    float MaxDriveClosingSpeed = 50.0f;
    float MaxDriveClosingAngularSpeed = 30.0f;
};

struct BodyDesc
{
    CollisionShape Shape;
    CollisionShapeHandle MeshShape;
    Vec3d Position = Vec3d::Zero();
    Quatf Rotation = Quatf::Identity();
    BodyMotion Motion = BodyMotion::Static;
    CollisionLayer Layer = CollisionLayer::Moving;
    float Mass = 1.0f;
    bool IsTrigger = false;
    uint64_t UserData = 0;

    float GravityScale = 1.0f;
    float LinearDamping = 0.05f;
    float AngularDamping = 0.05f;
};

class PhysicsWorld
{
public:
    explicit PhysicsWorld(const PhysicsWorldConfig& config = {});
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    void SetShapeCache(const CollisionShapeCache* cache);

    void Step(float dt, int collisionSteps = 1);

    // --- Body interface ------------------------------------------------------
    [[nodiscard]] PhysicsBodyId AddBody(const BodyDesc& desc);
    void RemoveBody(PhysicsBodyId id);

    [[nodiscard]] BodyTransform GetBodyTransform(PhysicsBodyId id) const;
    void SetBodyTransform(PhysicsBodyId id, const Vec3d& position, const Quatf& rotation);

    [[nodiscard]] Vec3d GetLinearVelocity(PhysicsBodyId id) const;
    void SetLinearVelocity(PhysicsBodyId id, const Vec3d& velocity);

    [[nodiscard]] Vec3d GetAngularVelocity(PhysicsBodyId id) const;
    void SetAngularVelocity(PhysicsBodyId id, const Vec3d& velocity);

    [[nodiscard]] float GetGravityScale(PhysicsBodyId id) const;
    void SetGravityScale(PhysicsBodyId id, float scale);

    [[nodiscard]] bool IsBodyActive(PhysicsBodyId id) const;
    void WakeBody(PhysicsBodyId id);

    [[nodiscard]] uint64_t GetUserData(PhysicsBodyId id) const;
    [[nodiscard]] uint32_t BodyCount() const;

    // --- Driven pose constraints -------------------------------------------
    // Targets are refreshed for every live constraint before each step. An
    // unrefreshed constraint is an orchestration bug: debug builds assert;
    // release builds skip it and count the miss.
    [[nodiscard]] PhysicsConstraintId AddDrivenPoseConstraint(const DrivenPoseConstraintDesc& desc);
    void RemoveConstraint(PhysicsConstraintId id);
    [[nodiscard]] bool IsConstraintValid(PhysicsConstraintId id) const;

    void SetDrivenPoseTarget(PhysicsConstraintId id, const DrivenPoseTarget& target);
    [[nodiscard]] PhysicsConstraintTelemetry GetConstraintTelemetry(PhysicsConstraintId id) const;

    [[nodiscard]] uint32_t ConstraintCount() const;
    [[nodiscard]] uint64_t StaleRefreshCount() const;

    [[nodiscard]] PhysicsWorldImpl& Internal() { return *Impl; }
    [[nodiscard]] const PhysicsWorldImpl& Internal() const { return *Impl; }

private:
    void DriveConstraints(float dt);

    std::unique_ptr<PhysicsWorldImpl> Impl;
    const CollisionShapeCache* ShapeCache = nullptr;
};
