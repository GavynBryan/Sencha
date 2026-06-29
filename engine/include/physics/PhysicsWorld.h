#pragma once

#include <cstdint>
#include <memory>

#include <math/Quat.h>
#include <math/Vec.h>
#include <physics/CollisionShape.h>
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
// lives in PhysicsScene, read-only spatial queries in PhysicsQueries, frame
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
};

struct BodyTransform
{
    Vec3d Position;
    Quatf Rotation;
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

    // --- Body interface (used by PhysicsScene, not gameplay) ---------------
    [[nodiscard]] PhysicsBodyId AddBody(const BodyDesc& desc);
    void RemoveBody(PhysicsBodyId id);

    [[nodiscard]] BodyTransform GetBodyTransform(PhysicsBodyId id) const;
    void SetBodyTransform(PhysicsBodyId id, const Vec3d& position, const Quatf& rotation);

    [[nodiscard]] Vec3d GetLinearVelocity(PhysicsBodyId id) const;
    void SetLinearVelocity(PhysicsBodyId id, const Vec3d& velocity);

    [[nodiscard]] uint64_t GetUserData(PhysicsBodyId id) const;
    [[nodiscard]] uint32_t BodyCount() const;

    // Backend access for in-module collaborators (queries, scene, character).
    // Returns an incomplete type here; only physics .cpp files that include the
    // internal header can use it, which is the firewall.
    [[nodiscard]] PhysicsWorldImpl& Internal() { return *Impl; }
    [[nodiscard]] const PhysicsWorldImpl& Internal() const { return *Impl; }

private:
    std::unique_ptr<PhysicsWorldImpl> Impl;
    const CollisionShapeCache* ShapeCache = nullptr;
};
