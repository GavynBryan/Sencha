#include <physics/PhysicsScene.h>

#include <ecs/World.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/Collider.h>
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
// for the parented case.
BodyTransform ReadPose(const World& world, EntityId entity)
{
    if (world.IsRegistered<WorldTransform>())
        if (const WorldTransform* wt = world.TryGet<WorldTransform>(entity))
            return BodyTransform{ wt->Value.Position, wt->Value.Rotation };
    if (world.IsRegistered<LocalTransform>())
        if (const LocalTransform* lt = world.TryGet<LocalTransform>(entity))
            return BodyTransform{ lt->Value.Position, lt->Value.Rotation };
    return BodyTransform{ Vec3d::Zero(), Quatf::Identity() };
}
} // namespace

PhysicsScene::PhysicsScene(PhysicsWorld& world)
    : Simulation(&world)
{
}

PhysicsScene::~PhysicsScene()
{
    // Safe without a lifetime guard: the world outlives this scene (zones are
    // destroyed before the step system that owns the world).
    for (const auto& [index, body] : Bodies)
        Simulation->RemoveBody(body);
}

void PhysicsScene::SyncToPhysics(World& world)
{
    if (!world.IsRegistered<Collider>())
        return; // a zone with no colliders registered has no bodies to sync

    const World& readOnly = world; // const iteration: do not mark colliders changed
    const bool hasRigidBody = readOnly.IsRegistered<RigidBody>();
    Seen.clear();

    readOnly.ForEachComponent<Collider>([&](EntityId entity, const Collider& collider)
    {
        Seen.insert(entity.Index);
        const RigidBody* body = hasRigidBody ? readOnly.TryGet<RigidBody>(entity) : nullptr;
        const BodyMotion motion = body != nullptr ? body->Motion : BodyMotion::Static;

        auto existing = Bodies.find(entity.Index);
        if (existing == Bodies.end())
        {
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
            if (id.IsValid())
            {
                Bodies.emplace(entity.Index, id);
                if (body != nullptr)
                    Simulation->SetLinearVelocity(id, body->LinearVelocity);
            }
        }
        else if (motion == BodyMotion::Kinematic)
        {
            const BodyTransform pose = ReadPose(readOnly, entity);
            Simulation->SetBodyTransform(existing->second, pose.Position, pose.Rotation);
        }
    });

    for (auto it = Bodies.begin(); it != Bodies.end();)
    {
        if (Seen.find(it->first) == Seen.end())
        {
            Simulation->RemoveBody(it->second);
            it = Bodies.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void PhysicsScene::SyncFromPhysics(World& world)
{
    if (!world.IsRegistered<RigidBody>())
        return; // no dynamic bodies to write back

    const bool hasLocal = world.IsRegistered<LocalTransform>();
    world.ForEachComponent<RigidBody>([&](EntityId entity, RigidBody& body)
    {
        if (body.Motion != BodyMotion::Dynamic)
            return;
        auto it = Bodies.find(entity.Index);
        if (it == Bodies.end())
            return;

        const BodyTransform pose = Simulation->GetBodyTransform(it->second);
        if (hasLocal)
        {
            if (LocalTransform* local = world.TryGet<LocalTransform>(entity))
            {
                local->Value.Position = pose.Position;
                local->Value.Rotation = pose.Rotation;
            }
        }
        body.LinearVelocity = Simulation->GetLinearVelocity(it->second);
    });
}
