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

// The body carries no scale; its shape takes the entity's instead, read once
// when the body is built.
Vec3d ReadScale(const World& world, EntityId entity)
{
    if (world.IsRegistered<WorldTransform>())
    {
        if (const WorldTransform* transform =
                world.TryGet<WorldTransform>(entity))
            return transform->Value.Scale;
    }
    if (const LocalTransform* transform =
            world.TryGet<LocalTransform>(entity))
        return transform->Value.Scale;
    return Vec3d::One();
}

BodyTransform InParentFrame(const World& world, EntityId entity, const BodyTransform& pose)
{
    if (!world.IsRegistered<Parent>() || !world.IsRegistered<WorldTransform>())
        return pose;
    const Parent* parent = world.TryGet<Parent>(entity);
    const WorldTransform* frame = parent != nullptr ? world.TryGet<WorldTransform>(parent->Entity) : nullptr;
    if (frame == nullptr)
        return pose;
    return BodyTransform{ frame->Value.InverseTransformPoint(pose.Position),
                          (frame->Value.Rotation.Conjugate() * pose.Rotation).Normalized() };
}

void PullDynamicState(const PhysicsWorld& simulation, const World& world, EntityId entity, PhysicsBodyId id,
                      LocalTransform& transform, RigidBody& body)
{
    const bool awake = simulation.IsBodyActive(id);
    if (!awake && body.Asleep)
        return;
    const BodyTransform pose = InParentFrame(world, entity, simulation.GetBodyTransform(id));
    transform.Value.Position = pose.Position;
    transform.Value.Rotation = pose.Rotation;
    body.LinearVelocity = awake ? simulation.GetLinearVelocity(id) : Vec3d::Zero();
    body.AngularVelocity = awake ? simulation.GetAngularVelocity(id) : Vec3d::Zero();
    body.Asleep = !awake;
}

} // namespace

struct RigidBodyBinding::SceneState
{
    explicit SceneState(World& world)
        : Commands(world)
        , KinematicPush(world)
        , DynamicPull(world)
        , SensorPush(world)
    {
    }

    CommandBuffer Commands;
    Query<Read<LocalTransform>, Read<RigidBody>, Read<PhysicsBodyLink>>
        KinematicPush;
    Query<Write<LocalTransform>, Write<RigidBody>, Read<PhysicsBodyLink>>
        DynamicPull;
    Query<Read<LocalTransform>, Read<Collider>, Read<PhysicsBodyLink>> SensorPush;
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

    PullDynamicState(*Simulation, world, record.Entity, record.Body, *transform, *body);
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
        desc.Shape = ScaledCollisionShape(collider.Shape, ReadScale(readOnly, entity));
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
            desc.StartAsleep = body->Asleep;
        }

        const PhysicsBodyId id = Simulation->AddBody(desc);
        if (!id.IsValid())
            return;

        Owned.push_back(BodyRecord{ entity, id });
        if (body != nullptr && !body->Asleep)
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

    const auto leaving = [&](const BodyRecord& record) {
        const bool alive = world.IsAlive(record.Entity);
        return !alive || !world.HasComponent<Collider>(record.Entity)
            || !partitions.Contains(world.GetEntityPartition(record.Entity));
    };
    for (const BodyRecord& record : Owned)
        if (leaving(record))
            CaptureDynamicState(world, record);
    for (size_t index = 0; index < Owned.size();)
    {
        const BodyRecord record = Owned[index];
        const bool alive = world.IsAlive(record.Entity);
        if (leaving(record))
        {
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
    state.ActivePartitions = partitions;
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

    // Bodies exist only for entities in the active set, so only that set's
    // structural churn can change what needs binding. A spawn or despawn in a
    // dormant zone leaves this scan skipped. The version is re-read after
    // Reconcile because Reconcile's own PhysicsBodyLink writes bump it.
    SceneState& state = EnsureState(world);
    if (world.StructuralVersion(partitions) != LastStructuralVersion
        || !(state.ActivePartitions == partitions))
    {
        Reconcile(world, state, partitions);
        LastStructuralVersion = world.StructuralVersion(partitions);
    }

    const World& readOnly = world;
    state.KinematicPush.ForEachChunkIn(partitions, [&](auto& view)
    {
        const auto bodies = view.template Read<RigidBody>();
        const auto links = view.template Read<PhysicsBodyLink>();
        for (uint32_t index = 0; index < view.Count(); ++index)
        {
            if (bodies[index].Motion == BodyMotion::Kinematic)
            {
                const BodyTransform wanted = ReadPose(readOnly, view.Entity(index));
                Simulation->SetBodyTransform(links[index].Body, wanted.Position, wanted.Rotation);
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

    if (Simulation->SensorCount() == 0)
        return;
    state.SensorPush.ForEachChunkIn(partitions, [&](auto& view)
    {
        const auto colliders = view.template Read<Collider>();
        const auto links = view.template Read<PhysicsBodyLink>();
        for (uint32_t index = 0; index < view.Count(); ++index)
        {
            if (!colliders[index].IsTrigger)
                continue;
            const auto* body = readOnly.TryGet<RigidBody>(view.Entity(index));
            if (body != nullptr && body->Motion != BodyMotion::Static)
                continue;
            const BodyTransform wanted = ReadPose(readOnly, view.Entity(index));
            const BodyTransform current = Simulation->GetBodyTransform(links[index].Body);
            if (wanted.Position != current.Position || wanted.Rotation != current.Rotation)
                Simulation->SetBodyTransform(links[index].Body, wanted.Position, wanted.Rotation);
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
            if (bodies[index].Motion == BodyMotion::Dynamic)
                PullDynamicState(*Simulation, world, view.Entity(index), links[index].Body, transforms[index],
                                 bodies[index]);
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
    const auto inPartition = [&](const BodyRecord& record) {
        return world.IsAlive(record.Entity) && world.GetEntityPartition(record.Entity) == partition;
    };
    for (const BodyRecord& record : Owned)
        if (inPartition(record))
            CaptureDynamicState(world, record);
    for (size_t index = 0; index < Owned.size();)
    {
        const BodyRecord record = Owned[index];
        if (!inPartition(record))
        {
            ++index;
            continue;
        }

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
    LastStructuralVersion = world.StructuralVersion(state.ActivePartitions);
}

void RigidBodyBinding::EvictAll(World& world)
{
    if (!Ready(world) || Owned.empty())
        return;

    SceneState& state = EnsureState(world);
    for (const BodyRecord& record : Owned)
        CaptureDynamicState(world, record);
    for (const BodyRecord& record : Owned)
    {
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
    LastStructuralVersion = world.StructuralVersion(state.ActivePartitions);
}
