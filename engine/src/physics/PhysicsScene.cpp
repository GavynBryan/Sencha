#include <physics/PhysicsScene.h>

#include <cassert>

#include <ecs/CommandBuffer.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <world/transform/TransformComponents.h>

namespace
{
CollisionLayer DeriveLayer(BodyMotion motion, bool isTrigger)
{
    if (isTrigger)
        return CollisionLayer::Trigger;
    return motion == BodyMotion::Static ? CollisionLayer::Static : CollisionLayer::Moving;
}

// Place bodies at the entity's world pose. Propagation runs after physics, so a
// top-level entity (the MVP case: brush cells, player, loose dynamics) has
// LocalTransform == WorldTransform here; WorldTransform is preferred when present
// for the parented case. Used only at body creation (the rare path).
BodyTransform ReadPose(const World& world, EntityId entity)
{
    if (world.IsRegistered<WorldTransform>())
        if (const WorldTransform* wt = world.TryGet<WorldTransform>(entity))
            return BodyTransform{ wt->Value.Position, wt->Value.Rotation };
    if (const LocalTransform* lt = world.TryGet<LocalTransform>(entity))
        return BodyTransform{ lt->Value.Position, lt->Value.Rotation };
    return BodyTransform{ Vec3d::Zero(), Quatf::Identity() };
}
} // namespace

// PIMPL: keeps Query.h out of the public header, matching the Jolt firewall
// discipline. The queries are cached (built once, bound to this scene's World)
// and the command buffer is reused (Flush clears it).
struct PhysicsScene::SceneState
{
    explicit SceneState(World& world)
        : Commands(world)
        , KinematicPush(world)
        , DynamicPull(world)
    {
    }

    CommandBuffer Commands;
    Query<Read<LocalTransform>, Read<RigidBody>, Read<PhysicsBodyLink>>   KinematicPush;
    Query<Write<LocalTransform>, Write<RigidBody>, Read<PhysicsBodyLink>> DynamicPull;
};

PhysicsScene::PhysicsScene(PhysicsWorld& world)
    : Simulation(&world)
{
}

PhysicsScene::~PhysicsScene()
{
    // Safe without a lifetime guard: the world outlives this scene (zones are
    // destroyed before the step system that owns the world).
    for (const BodyRecord& rec : Owned)
        Simulation->RemoveBody(rec.Body);
}

bool PhysicsScene::Ready(const World& world) const
{
    // The bridge needs colliders to bind, the link component to mark bound
    // bodies, and a transform to place them. RigidBody gates the dynamic and
    // kinematic queries; RegisterPhysicsComponents registers all of these
    // together, so a configured zone passes and a bare World stays inert.
    return world.IsRegistered<Collider>() && world.IsRegistered<RigidBody>()
        && world.IsRegistered<PhysicsBodyLink>() && world.IsRegistered<LocalTransform>();
}

PhysicsScene::SceneState& PhysicsScene::EnsureState(World& world)
{
    if (!State)
        State = std::make_unique<SceneState>(world);
    return *State;
}

void PhysicsScene::Reconcile(World& world, SceneState& state)
{
    ++ReconcileCount;
    const World& readOnly = world; // const iteration: do not mark colliders changed

    // Create a body for every collider that does not have one yet. ForEachComponent
    // yields the full (generational) EntityId the body's UserData and the Owned
    // record need; the HasComponent skip keeps already-bound colliders untouched.
    readOnly.ForEachComponent<Collider>([&](EntityId entity, const Collider& collider)
    {
        if (world.HasComponent<PhysicsBodyLink>(entity))
            return;

        const RigidBody* body = readOnly.TryGet<RigidBody>(entity);
        const BodyMotion motion = body != nullptr ? body->Motion : BodyMotion::Static;
        const BodyTransform pose = ReadPose(readOnly, entity);

        BodyDesc desc;
        desc.Shape = collider.Shape;
        desc.MeshShape = collider.Mesh;
        desc.Position = pose.Position;
        desc.Rotation = pose.Rotation;
        desc.Motion = motion;
        desc.Layer = DeriveLayer(motion, collider.IsTrigger);
        desc.IsTrigger = collider.IsTrigger;
        desc.Mass = body != nullptr ? body->Mass : 1.0f;
        desc.UserData = PackEntity(entity);

        const PhysicsBodyId id = Simulation->AddBody(desc);
        if (!id.IsValid())
            return;

        Owned.push_back(BodyRecord{ entity, id });
        if (body != nullptr)
            Simulation->SetLinearVelocity(id, body->LinearVelocity);
        state.Commands.AddComponent<PhysicsBodyLink>(entity, PhysicsBodyLink{ id });
    });

    // Drop bodies whose entity was destroyed (no hook fired; the link vanished
    // with the entity) or whose collider was removed. One sweep over Owned, so
    // both cases are covered; only the collider-removed case still has a link to
    // strip. O(bodies in this zone), paid only on structural-change frames.
    for (size_t i = 0; i < Owned.size();)
    {
        const EntityId entity = Owned[i].Entity;
        const bool alive = world.IsAlive(entity);
        const bool hasCollider = alive && world.HasComponent<Collider>(entity);
        if (!alive || !hasCollider)
        {
            Simulation->RemoveBody(Owned[i].Body);
            if (alive)
                state.Commands.RemoveComponent<PhysicsBodyLink>(entity);
            Owned[i] = Owned.back();
            Owned.pop_back();
        }
        else
        {
            ++i;
        }
    }

    state.Commands.Flush();
}

void PhysicsScene::SyncToPhysics(World& world)
{
    // The likely misconfiguration: colliders registered but the runtime link
    // forgotten, which would re-create every body each step. Loud in debug;
    // a fully bare World (no colliders) stays silently inert.
    assert(!(world.IsRegistered<Collider>() && !world.IsRegistered<PhysicsBodyLink>())
           && "Collider registered without PhysicsBodyLink: call RegisterPhysicsComponents.");

    if (!Ready(world))
        return;

    SceneState& state = EnsureState(world);

    const uint64_t version = world.StructuralVersion();
    if (version != LastStructuralVersion)
    {
        Reconcile(world, state);
        LastStructuralVersion = world.StructuralVersion(); // post-flush value
    }

    // Push authored transforms into kinematic bodies. Reads only (no version
    // bump). Kinematic bodies are few; static colliders carry no RigidBody and
    // so never match.
    state.KinematicPush.ForEachChunk([&](auto& view)
    {
        const auto transforms = view.template Read<LocalTransform>();
        const auto bodies = view.template Read<RigidBody>();
        const auto links = view.template Read<PhysicsBodyLink>();
        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            if (bodies[i].Motion != BodyMotion::Kinematic)
                continue;
            Simulation->SetBodyTransform(
                links[i].Body, transforms[i].Value.Position, transforms[i].Value.Rotation);
        }
    });
}

void PhysicsScene::SyncFromPhysics(World& world)
{
    if (!Ready(world) || Owned.empty())
        return;

    SceneState& state = EnsureState(world);

    // Write resolved transforms and velocity back for dynamic bodies. Contiguous
    // column walk, no hashing. Static colliders (no RigidBody) and kinematic
    // bodies (skipped) are not written.
    state.DynamicPull.ForEachChunk([&](auto& view)
    {
        auto transforms = view.template Write<LocalTransform>();
        auto bodies = view.template Write<RigidBody>();
        const auto links = view.template Read<PhysicsBodyLink>();
        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            if (bodies[i].Motion != BodyMotion::Dynamic)
                continue;
            const BodyTransform pose = Simulation->GetBodyTransform(links[i].Body);
            transforms[i].Value.Position = pose.Position;
            transforms[i].Value.Rotation = pose.Rotation;
            bodies[i].LinearVelocity = Simulation->GetLinearVelocity(links[i].Body);
        }
    });
}
