#include <physics/RigidBodyBinding.h>

#include <cassert>
#include <cmath>

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

struct RigidBodyBinding::SceneState
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

RigidBodyBinding::RigidBodyBinding(PhysicsWorld& world)
    : Simulation(&world)
{
}

RigidBodyBinding::~RigidBodyBinding()
{
    for (const BodyRecord& rec : Owned)
        Simulation->RemoveBody(rec.Body);
}

bool RigidBodyBinding::Ready(const World& world) const
{
    return world.IsRegistered<Collider>() && world.IsRegistered<RigidBody>()
        && world.IsRegistered<PhysicsBodyLink>() && world.IsRegistered<LocalTransform>();
}

RigidBodyBinding::SceneState& RigidBodyBinding::EnsureState(World& world)
{
    if (!State)
        State = std::make_unique<SceneState>(world);
    return *State;
}

void RigidBodyBinding::Reconcile(World& world, SceneState& state)
{
    ++ReconcileCount;
    const World& readOnly = world;

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
        if (body != nullptr)
        {
            desc.GravityScale = body->GravityScale;
            desc.LinearDamping = body->LinearDamping;
            desc.AngularDamping = body->AngularDamping;
        }

        const PhysicsBodyId id = Simulation->AddBody(desc);
        if (!id.IsValid())
            return;

        Owned.push_back(BodyRecord{ entity, id });
        if (body != nullptr)
        {
            Simulation->SetLinearVelocity(id, body->LinearVelocity);
            Simulation->SetAngularVelocity(id, body->AngularVelocity);
        }
        state.Commands.AddComponent<PhysicsBodyLink>(entity, PhysicsBodyLink{ id });
    });

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

void RigidBodyBinding::SyncToPhysics(World& world)
{
    assert(!(world.IsRegistered<Collider>() && !world.IsRegistered<PhysicsBodyLink>())
           && "Collider registered without PhysicsBodyLink: call RegisterPhysicsComponents.");

    if (!Ready(world))
        return;

    SceneState& state = EnsureState(world);

    const uint64_t version = world.StructuralVersion();
    if (version != LastStructuralVersion)
    {
        Reconcile(world, state);
        LastStructuralVersion = world.StructuralVersion();
    }

    state.KinematicPush.ForEachChunk([&](auto& view)
    {
        const auto transforms = view.template Read<LocalTransform>();
        const auto bodies = view.template Read<RigidBody>();
        const auto links = view.template Read<PhysicsBodyLink>();
        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            if (bodies[i].Motion == BodyMotion::Kinematic)
            {
                Simulation->SetBodyTransform(
                    links[i].Body, transforms[i].Value.Position, transforms[i].Value.Rotation);
            }
            else if (bodies[i].Motion == BodyMotion::Dynamic)
            {
                // SetGravityFactor does not wake a sleeping body. Compare the
                // applied backend value so a runtime edit wakes exactly once,
                // while unchanged bodies remain eligible for sleep.
                const float applied = Simulation->GetGravityScale(links[i].Body);
                if (std::abs(applied - bodies[i].GravityScale) > 1e-6f)
                {
                    Simulation->SetGravityScale(links[i].Body, bodies[i].GravityScale);
                    Simulation->WakeBody(links[i].Body);
                }
            }
        }
    });
}

void RigidBodyBinding::Evict(World& world)
{
    if (!Ready(world) || Owned.empty())
        return;

    SyncFromPhysics(world);

    SceneState& state = EnsureState(world);
    for (const BodyRecord& rec : Owned)
    {
        Simulation->RemoveBody(rec.Body);
        if (world.IsAlive(rec.Entity) && world.HasComponent<PhysicsBodyLink>(rec.Entity))
            state.Commands.RemoveComponent<PhysicsBodyLink>(rec.Entity);
    }
    Owned.clear();
    state.Commands.Flush();
}

void RigidBodyBinding::SyncFromPhysics(World& world)
{
    if (!Ready(world) || Owned.empty())
        return;

    SceneState& state = EnsureState(world);

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
            bodies[i].AngularVelocity = Simulation->GetAngularVelocity(links[i].Body);
        }
    });
}
