#include <physics/RigidBodyBinding.h>

#include <cassert>
#include <cmath>

#include <ecs/CommandBuffer.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
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
    return motion == BodyMotion::Static
        ? CollisionLayer::Static
        : CollisionLayer::Moving;
}

BodyTransform ReadPose(const World& world, EntityId entity)
{
    if (world.IsRegistered<WorldTransform>())
    {
        if (const WorldTransform* transform =
                world.TryGet<WorldTransform>(entity))
        {
            return BodyTransform{
                transform->Value.Position,
                transform->Value.Rotation,
            };
        }
    }

    if (const LocalTransform* transform =
            world.TryGet<LocalTransform>(entity))
    {
        return BodyTransform{
            transform->Value.Position,
            transform->Value.Rotation,
        };
    }
    return BodyTransform{ Vec3d::Zero(), Quatf::Identity() };
}

bool SamePartitions(
    const StoragePartitionSet& a,
    const StoragePartitionSet& b)
{
    if (a.Size() != b.Size())
        return false;
    for (StoragePartitionId partition : a.Members())
        if (!b.Contains(partition))
            return false;
    return true;
}

void CopyPartitions(
    StoragePartitionSet& destination,
    const StoragePartitionSet& source)
{
    destination.Clear();
    for (StoragePartitionId partition : source.Members())
        destination.Add(partition);
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
    Query<Read<LocalTransform>, Read<RigidBody>, Read<PhysicsBodyLink>>
        KinematicPush;
    Query<Write<LocalTransform>, Write<RigidBody>, Read<PhysicsBodyLink>>
        DynamicPull;
    StoragePartitionSet ActivePartitions;
};

RigidBodyBinding::RigidBodyBinding(PhysicsWorld& world)
    : Simulation(&world)
{
}

RigidBodyBinding::~RigidBodyBinding()
{
    for (const BodyRecord& record : Owned)
        Simulation->RemoveBody(record.Body);
}

bool RigidBodyBinding::Ready(const World& world) const
{
    return world.IsRegistered<Collider>()
        && world.IsRegistered<RigidBody>()
        && world.IsRegistered<PhysicsBodyLink>()
        && world.IsRegistered<LocalTransform>();
}

RigidBodyBinding::SceneState& RigidBodyBinding::EnsureState(World& world)
{
    if (!State)
        State = std::make_unique<SceneState>(world);
    return *State;
}

void RigidBodyBinding::CaptureDynamicState(
    World& world,
    const BodyRecord& record)
{
    if (!world.IsAlive(record.Entity))
        return;

    RigidBody* body = world.TryGet<RigidBody>(record.Entity);
    LocalTransform* transform =
        world.TryGet<LocalTransform>(record.Entity);
    if (body == nullptr || transform == nullptr
        || body->Motion != BodyMotion::Dynamic)
    {
        return;
    }

    const BodyTransform pose =
        Simulation->GetBodyTransform(record.Body);
    transform->Value.Position = pose.Position;
    transform->Value.Rotation = pose.Rotation;
    body->LinearVelocity =
        Simulation->GetLinearVelocity(record.Body);
    body->AngularVelocity =
        Simulation->GetAngularVelocity(record.Body);
}

void RigidBodyBinding::Reconcile(
    World& world,
    SceneState& state,
    const StoragePartitionSet& partitions)
{
    ++ReconcileCount;
    const World& readOnly = world;

    readOnly.ForEachComponent<Collider>(
        [&](EntityId entity, const Collider& collider)
    {
        if (!partitions.Contains(world.GetEntityPartition(entity))
            || world.HasComponent<PhysicsBodyLink>(entity))
        {
            return;
        }

        const RigidBody* body = readOnly.TryGet<RigidBody>(entity);
        const BodyMotion motion = body != nullptr
            ? body->Motion
            : BodyMotion::Static;
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
            Simulation->SetLinearVelocity(
                id,
                body->LinearVelocity);
            Simulation->SetAngularVelocity(
                id,
                body->AngularVelocity);
        }
        state.Commands.AddComponent<PhysicsBodyLink>(
            entity,
            PhysicsBodyLink{ id });
    });

    for (size_t index = 0; index < Owned.size();)
    {
        const BodyRecord record = Owned[index];
        const bool alive = world.IsAlive(record.Entity);
        const bool hasCollider =
            alive && world.HasComponent<Collider>(record.Entity);
        const bool active = alive
            && partitions.Contains(
                world.GetEntityPartition(record.Entity));
        if (!alive || !hasCollider || !active)
        {
            CaptureDynamicState(world, record);
            Simulation->RemoveBody(record.Body);
            if (alive
                && world.HasComponent<PhysicsBodyLink>(record.Entity))
            {
                state.Commands.RemoveComponent<PhysicsBodyLink>(
                    record.Entity);
            }
            Owned[index] = Owned.back();
            Owned.pop_back();
            continue;
        }
        ++index;
    }

    state.Commands.Flush();
    CopyPartitions(state.ActivePartitions, partitions);
}

void RigidBodyBinding::SyncToPhysics(
    World& world,
    const StoragePartitionSet& partitions)
{
    assert(!(world.IsRegistered<Collider>()
             && !world.IsRegistered<PhysicsBodyLink>())
           && "Collider registered without PhysicsBodyLink");

    if (!Ready(world))
        return;

    SceneState& state = EnsureState(world);
    const uint64_t version = world.StructuralVersion();
    if (version != LastStructuralVersion
        || !SamePartitions(state.ActivePartitions, partitions))
    {
        Reconcile(world, state, partitions);
        LastStructuralVersion = world.StructuralVersion();
    }

    state.KinematicPush.ForEachChunkIn(partitions, [&](auto& view)
    {
        const auto transforms = view.template Read<LocalTransform>();
        const auto bodies = view.template Read<RigidBody>();
        const auto links = view.template Read<PhysicsBodyLink>();
        for (uint32_t index = 0; index < view.Count(); ++index)
        {
            if (bodies[index].Motion == BodyMotion::Kinematic)
            {
                Simulation->SetBodyTransform(
                    links[index].Body,
                    transforms[index].Value.Position,
                    transforms[index].Value.Rotation);
            }
            else if (bodies[index].Motion == BodyMotion::Dynamic)
            {
                const float applied =
                    Simulation->GetGravityScale(links[index].Body);
                if (std::abs(applied - bodies[index].GravityScale)
                    > 1e-6f)
                {
                    Simulation->SetGravityScale(
                        links[index].Body,
                        bodies[index].GravityScale);
                    Simulation->WakeBody(links[index].Body);
                }
            }
        }
    });
}

void RigidBodyBinding::SyncFromPhysics(
    World& world,
    const StoragePartitionSet& partitions)
{
    if (!Ready(world) || Owned.empty())
        return;

    SceneState& state = EnsureState(world);
    state.DynamicPull.ForEachChunkIn(partitions, [&](auto& view)
    {
        auto transforms = view.template Write<LocalTransform>();
        auto bodies = view.template Write<RigidBody>();
        const auto links = view.template Read<PhysicsBodyLink>();
        for (uint32_t index = 0; index < view.Count(); ++index)
        {
            if (bodies[index].Motion != BodyMotion::Dynamic)
                continue;

            const BodyTransform pose =
                Simulation->GetBodyTransform(links[index].Body);
            transforms[index].Value.Position = pose.Position;
            transforms[index].Value.Rotation = pose.Rotation;
            bodies[index].LinearVelocity =
                Simulation->GetLinearVelocity(links[index].Body);
            bodies[index].AngularVelocity =
                Simulation->GetAngularVelocity(links[index].Body);
        }
    });
}

void RigidBodyBinding::EvictPartition(
    World& world,
    StoragePartitionId partition)
{
    if (!Ready(world) || Owned.empty())
        return;

    SceneState& state = EnsureState(world);
    for (size_t index = 0; index < Owned.size();)
    {
        const BodyRecord record = Owned[index];
        if (!world.IsAlive(record.Entity)
            || world.GetEntityPartition(record.Entity) != partition)
        {
            ++index;
            continue;
        }

        CaptureDynamicState(world, record);
        Simulation->RemoveBody(record.Body);
        if (world.HasComponent<PhysicsBodyLink>(record.Entity))
        {
            state.Commands.RemoveComponent<PhysicsBodyLink>(
                record.Entity);
        }
        Owned[index] = Owned.back();
        Owned.pop_back();
    }
    state.Commands.Flush();
    state.ActivePartitions.Remove(partition);
    LastStructuralVersion = world.StructuralVersion();
}

void RigidBodyBinding::EvictAll(World& world)
{
    if (!Ready(world) || Owned.empty())
        return;

    SceneState& state = EnsureState(world);
    for (const BodyRecord& record : Owned)
    {
        CaptureDynamicState(world, record);
        Simulation->RemoveBody(record.Body);
        if (world.IsAlive(record.Entity)
            && world.HasComponent<PhysicsBodyLink>(record.Entity))
        {
            state.Commands.RemoveComponent<PhysicsBodyLink>(
                record.Entity);
        }
    }
    Owned.clear();
    state.Commands.Flush();
    state.ActivePartitions.Clear();
    LastStructuralVersion = world.StructuralVersion();
}
