#include <physics/CharacterMoverPool.h>

#include <optional>
#include <vector>

#include <ecs/CommandBuffer.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <physics/CharacterMover.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <world/transform/TransformComponents.h>

namespace
{
Vec3d ReadPosition(const World& world, EntityId entity)
{
    if (const LocalTransform* lt = world.TryGet<LocalTransform>(entity))
        return lt->Value.Position;
    return Vec3d::Zero();
}
} // namespace

// PIMPL: dense mover pool with a free list, plus the cached drive query and the
// reusable command buffer. Kept in the .cpp so the public header stays free of
// Query.h and CharacterMover.h.
struct CharacterMoverPool::State
{
    explicit State(World& world)
        : Commands(world)
        , Drive(world)
    {
    }

    struct Slot
    {
        EntityId                     Owner;
        std::optional<CharacterMover> Mover;
    };

    std::vector<Slot>     Slots;
    std::vector<uint32_t> Free;
    CommandBuffer         Commands;
    Query<Write<CharacterController>, Write<LocalTransform>, Read<CharacterMoverLink>> Drive;

    uint32_t Allocate(EntityId owner)
    {
        if (!Free.empty())
        {
            const uint32_t slot = Free.back();
            Free.pop_back();
            Slots[slot].Owner = owner;
            return slot;
        }
        Slots.push_back(Slot{ owner, std::nullopt });
        return static_cast<uint32_t>(Slots.size() - 1);
    }

    void Release(uint32_t slot)
    {
        Slots[slot].Mover.reset();
        Slots[slot].Owner = EntityId{};
        Free.push_back(slot);
    }
};

CharacterMoverPool::CharacterMoverPool(PhysicsWorld& world)
    : Simulation(&world)
{
}

CharacterMoverPool::~CharacterMoverPool() = default;

size_t CharacterMoverPool::MoverCount() const
{
    return S ? S->Slots.size() - S->Free.size() : 0;
}

bool CharacterMoverPool::Ready(const World& world) const
{
    return world.IsRegistered<CharacterController>()
        && world.IsRegistered<CharacterMoverLink>()
        && world.IsRegistered<LocalTransform>();
}

CharacterMoverPool::State& CharacterMoverPool::EnsureState(World& world)
{
    if (!S)
        S = std::make_unique<State>(world);
    return *S;
}

void CharacterMoverPool::Reconcile(World& world)
{
    if (!Ready(world))
        return;

    const uint64_t version = world.StructuralVersion();
    if (version == LastStructuralVersion)
        return;

    State& state = EnsureState(world);
    ++ReconcileCount;
    const World& readOnly = world;

    // Create a mover for every controller that does not have one yet.
    readOnly.ForEachComponent<CharacterController>([&](EntityId entity, const CharacterController& controller)
    {
        if (world.HasComponent<CharacterMoverLink>(entity))
            return;

        const uint32_t slot = state.Allocate(entity);
        const CharacterMoverConfig config{
            controller.Radius, controller.Height, controller.SlopeLimitDegrees, 70.0f };
        state.Slots[slot].Mover.emplace(*Simulation, config, ReadPosition(readOnly, entity));
        state.Commands.AddComponent<CharacterMoverLink>(entity, CharacterMoverLink{ slot });
    });

    // Release movers whose owner was destroyed (link gone with the entity) or
    // lost its controller. O(slots) on structural-change frames only.
    for (uint32_t slot = 0; slot < state.Slots.size(); ++slot)
    {
        if (!state.Slots[slot].Mover)
            continue;
        const EntityId owner = state.Slots[slot].Owner;
        const bool alive = world.IsAlive(owner);
        if (!alive || !world.HasComponent<CharacterController>(owner))
        {
            if (alive)
                state.Commands.RemoveComponent<CharacterMoverLink>(owner);
            state.Release(slot);
        }
    }

    state.Commands.Flush();
    LastStructuralVersion = world.StructuralVersion(); // post-flush value
}

void CharacterMoverPool::Drive(World& world, float dt, const Vec3d& gravity)
{
    if (!Ready(world) || !S)
        return;

    State& state = *S;
    state.Drive.ForEachChunk([&](auto& view)
    {
        auto controllers = view.template Write<CharacterController>();
        auto transforms = view.template Write<LocalTransform>();
        const auto links = view.template Read<CharacterMoverLink>();
        for (uint32_t i = 0; i < view.Count(); ++i)
        {
            CharacterMover& mover = *state.Slots[links[i].MoverSlot].Mover;
            // Jump is a one-shot request: only from the ground, then cleared so a
            // stale request cannot fire later. The mover owns the vertical impulse.
            if (controllers[i].PendingJumpSpeed > 0.0f && mover.IsGrounded())
                mover.Jump(controllers[i].PendingJumpSpeed);
            controllers[i].PendingJumpSpeed = 0.0f;
            mover.Move(
                Vec3d(controllers[i].DesiredVelocity.X, 0.0f, controllers[i].DesiredVelocity.Z),
                dt, gravity);
            controllers[i].Grounded = mover.IsGrounded();
            transforms[i].Value.Position = mover.GetPosition();
        }
    });
}
