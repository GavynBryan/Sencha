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
//
// Owns and advances one simulation, nothing more. It holds the backend (Jolt)
// instance and the scaffolding it cannot exist without (temp allocator, single-
// thread job system, layer filters) behind a PIMPL, so this header pulls in no
// backend type. ECS-free and renderer-free: tests construct it directly and
// step it without graphics.
//
// Responsibility boundary (see the physics plan): body lifetime/ECS binding
// lives in RigidBodyBinding, read-only spatial queries in PhysicsQueries, frame
// orchestration in PhysicsStepSystem. Those collaborators reach the backend
// through Internal(); gameplay never does.
//=============================================================================

struct PhysicsWorldImpl; // defined in src/physics/PhysicsWorldImpl.h (Jolt-side)
class CollisionShapeCache;

struct PhysicsWorldConfig
{
    Vec3d Gravity = Vec3d(0.0f, -9.81f, 0.0f);

    // Backend capacity hints. Sized for streamed room-scale zones; raise if a
    // game holds more simultaneous bodies. These bound allocations, not a hard
    // game limit beyond the backend's own.
    uint32_t MaxBodies = 10240;
    uint32_t MaxBodyPairs = 16384;
    uint32_t MaxContactConstraints = 8192;

    // Ceiling on the error-closing speed a driven pose constraint commands,
    // beyond the target frame's own velocity (which is never capped). Discrete
    // collision cannot resolve arbitrary closing speeds — an uncapped locked
    // drive tunnels through geometry when the error is large. 50 m/s crosses
    // ~0.8 m per 60 Hz step, within what room-scale geometry resolves; thin
    // geometry that needs more headroom is a motion-quality concern on the
    // body, not the constraint's.
    float MaxDriveClosingSpeed = 50.0f;
    float MaxDriveClosingAngularSpeed = 30.0f; // radians/second
};

// Everything needed to create one body. Backend-free: the shape is described by
// CollisionShape, the body is categorized by CollisionLayer, and UserData is an
// opaque tag the ECS bridge uses to carry the owning entity id.
struct BodyDesc
{
    // A valid MeshShape (a cooked shape in the bound CollisionShapeCache) takes
    // precedence over the inline primitive Shape.
    CollisionShape Shape;
    CollisionShapeHandle MeshShape;
    Vec3d Position = Vec3d::Zero();
    Quatf Rotation = Quatf::Identity();
    BodyMotion Motion = BodyMotion::Static;
    CollisionLayer Layer = CollisionLayer::Moving;
    float Mass = 1.0f; // dynamic only; <= 0 asks the backend to derive it from the shape
    bool IsTrigger = false;
    uint64_t UserData = 0;

    // Dynamic-body motion parameters; ignored for static bodies. Damping
    // defaults match the backend's, so omitting them changes nothing.
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

    // Bind the cache that resolves BodyDesc::MeshShape handles to cooked shapes.
    // Not owned. PhysicsStepSystem wires the shared cache here.
    void SetShapeCache(const CollisionShapeCache* cache);

    // Advance the simulation by dt seconds. collisionSteps subdivides dt for
    // contact stability; a fixed value keeps the step reproducible.
    void Step(float dt, int collisionSteps = 1);

    // --- Body interface (used by RigidBodyBinding, not gameplay) ---------------
    [[nodiscard]] PhysicsBodyId AddBody(const BodyDesc& desc);
    void RemoveBody(PhysicsBodyId id);

    [[nodiscard]] BodyTransform GetBodyTransform(PhysicsBodyId id) const;
    void SetBodyTransform(PhysicsBodyId id, const Vec3d& position, const Quatf& rotation);

    [[nodiscard]] Vec3d GetLinearVelocity(PhysicsBodyId id) const;
    void SetLinearVelocity(PhysicsBodyId id, const Vec3d& velocity);

    [[nodiscard]] Vec3d GetAngularVelocity(PhysicsBodyId id) const;
    void SetAngularVelocity(PhysicsBodyId id, const Vec3d& velocity);

    // Per-body gravity multiplier: 0 suspends, 1 is world gravity. Does not
    // wake a sleeping body — pair with WakeBody when a slept body must respond
    // to the change. (Velocity writes, by contrast, wake on their own.)
    void SetGravityScale(PhysicsBodyId id, float scale);

    void WakeBody(PhysicsBodyId id);

    [[nodiscard]] uint64_t GetUserData(PhysicsBodyId id) const;
    [[nodiscard]] uint32_t BodyCount() const;

    // --- Driven pose constraints (used by DrivenPoseBinding, not gameplay) --
    //
    // One-way: the follower is driven toward the target frame and keeps
    // colliding; nothing feeds back into whatever produced the frame. Targets
    // are per-step input — SetDrivenPoseTarget must be called between steps
    // for every live constraint. An unrefreshed constraint is an orchestration
    // bug: debug builds assert, release builds skip driving it for the step
    // and count it (StaleRefreshCount). RemoveBody invalidates constraints
    // referencing the removed body first, so a dependent constraint can never
    // dangle; removing an invalidated handle is a safe no-op in any order.

    [[nodiscard]] PhysicsConstraintId AddDrivenPoseConstraint(const DrivenPoseConstraintDesc& desc);
    void RemoveConstraint(PhysicsConstraintId id);
    [[nodiscard]] bool IsConstraintValid(PhysicsConstraintId id) const;

    void SetDrivenPoseTarget(PhysicsConstraintId id, const DrivenPoseTarget& target);
    [[nodiscard]] PhysicsConstraintTelemetry GetConstraintTelemetry(PhysicsConstraintId id) const;

    [[nodiscard]] uint32_t ConstraintCount() const;
    [[nodiscard]] uint64_t StaleRefreshCount() const;

    // Backend access for in-module collaborators (queries, scene, character).
    // Returns an incomplete type here; only physics .cpp files that include the
    // internal header can use it, which is the firewall.
    [[nodiscard]] PhysicsWorldImpl& Internal() { return *Impl; }
    [[nodiscard]] const PhysicsWorldImpl& Internal() const { return *Impl; }

private:
    void DriveConstraints(float dt);

    std::unique_ptr<PhysicsWorldImpl> Impl;
    const CollisionShapeCache* ShapeCache = nullptr;
};
