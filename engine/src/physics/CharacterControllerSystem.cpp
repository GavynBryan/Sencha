#include <physics/CharacterControllerSystem.h>

#include <unordered_map>

#include <app/GameContexts.h>
#include <ecs/World.h>
#include <physics/CharacterMover.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/CharacterController.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

namespace
{
// Per-registry persistent movers, keyed by entity. A World resource, so it dies
// with the zone and releases its CharacterVirtuals. (CharacterMover is movable,
// engine-typed; no Jolt leaks here.)
struct CharacterMoverStore
{
    std::unordered_map<EntityIndex, CharacterMover> Movers;
};

Vec3d ReadPosition(const World& world, EntityId entity)
{
    if (world.IsRegistered<LocalTransform>())
        if (const LocalTransform* lt = world.TryGet<LocalTransform>(entity))
            return lt->Value.Position;
    return Vec3d::Zero();
}
} // namespace

CharacterControllerSystem::CharacterControllerSystem(PhysicsStepSystem& step)
    : Step(&step)
{
}

void CharacterControllerSystem::Physics(PhysicsContext& ctx)
{
    PhysicsWorld& world = Step->GetSimulation();

    const float dt = static_cast<float>(ctx.Time.DeltaSeconds);

    for (Registry* reg : ctx.ActiveRegistries)
    {
        World& components = reg->Components;
        if (!components.IsRegistered<CharacterController>())
            continue;

        CharacterMoverStore& store = components.HasResource<CharacterMoverStore>()
            ? components.GetResource<CharacterMoverStore>()
            : components.AddResource<CharacterMoverStore>();

        components.ForEachComponent<CharacterController>([&](EntityId entity, CharacterController& controller)
        {
            auto it = store.Movers.find(entity.Index);
            if (it == store.Movers.end())
            {
                const CharacterMoverConfig config{
                    controller.Radius, controller.Height, controller.SlopeLimitDegrees, 70.0f };
                it = store.Movers.try_emplace(
                    entity.Index, world, config, ReadPosition(components, entity)).first;
            }

            CharacterMover& mover = it->second;
            mover.Move(Vec3d(controller.DesiredVelocity.X, 0.0f, controller.DesiredVelocity.Z), dt, Gravity);
            controller.Grounded = mover.IsGrounded();

            if (LocalTransform* local = components.TryGet<LocalTransform>(entity))
                local->Value.Position = mover.GetPosition();
        });
    }
}
