#include <physics/CharacterMoverPool.h>

#include <optional>
#include <vector>

#include <ecs/CommandBuffer.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
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
    if (const LocalTransform* transform =
            world.TryGet<LocalTransform>(entity))
    {
        return transform->Value.Position;
    }
    return Vec3d::Zero();
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

struct CharacterMoverPool::State
{
    explicit State(World& world)
        : Commands(world)
        , DriveQuery(world)
    {
    }

    struct Slot
    {
        EntityId Owner;
        std::optional<CharacterMover> Mover;
    };

    std::vector<Slot> Slots;
    std::vector<uint32_t> Free;
    CommandBuffer Commands;
    Query<
        Write<CharacterController>,
        Write<LocalTransform>,
        Read<CharacterMoverLink>> DriveQuery;
    StoragePartitionSet ActivePartitions;

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

void CharacterMoverPool::Reconcile(
    World& world,
    const StoragePartitionSet& partitions)
{
    if (!Ready(world))
        return;

    State& state = EnsureState(world);
    const uint64_t version = world.StructuralVersion();
    if (version == LastStructuralVersion
        && SamePartitions(state.ActivePartitions, partitions))
    {
        return;
    }

    ++ReconcileCount;
    const World& readOnly = world;

    readOnly.ForEachComponent<CharacterController>(
        [&](EntityId entity, const CharacterController& controller)
    {
        if (!partitions.Contains(world.GetEntityPartition(entity))
            || world.HasComponent<CharacterMoverLink>(entity))
        {
            return;
        }

        const uint32_t slot = state.Allocate(entity);
        const CharacterMoverConfig config{
            controller.Radius,
            controller.Height,
            controller.SlopeLimitDegrees,
            70.0f,
        };
        state.Slots[slot].Mover.emplace(
            *Simulation,
            config,
            ReadPosition(readOnly, entity));
        state.Commands.AddComponent<CharacterMoverLink>(
            entity,
            CharacterMoverLink{ slot });
    });

    for (uint32_t slot = 0; slot < state.Slots.size(); ++slot)
    {
        if (!state.Slots[slot].Mover)
            continue;

        const EntityId owner = state.Slots[slot].Owner;
        const bool alive = world.IsAlive(owner);
        const bool hasController = alive
            && world.HasComponent<CharacterController>(owner);
        const bool active = alive
            && partitions.Contains(world.GetEntityPartition(owner));
        if (alive && hasController && active)
            continue;

        if (alive && world.HasComponent<CharacterMoverLink>(owner))
            state.Commands.RemoveComponent<CharacterMoverLink>(owner);
        state.Release(slot);
    }

    state.Commands.Flush();
    CopyPartitions(state.ActivePartitions, partitions);
    LastStructuralVersion = world.StructuralVersion();
}

void CharacterMoverPool::Drive(
    World& world,
    const StoragePartitionSet& partitions,
    float dt,
    const Vec3d& gravity)
{
    if (!Ready(world) || !S)
        return;

    State& state = *S;
    state.DriveQuery.ForEachChunkIn(partitions, [&](auto& view)
    {
        auto controllers =
            view.template Write<CharacterController>();
        auto transforms = view.template Write<LocalTransform>();
        const auto links = view.template Read<CharacterMoverLink>();

        for (uint32_t index = 0; index < view.Count(); ++index)
        {
            CharacterMover& mover =
                *state.Slots[links[index].MoverSlot].Mover;
            if (controllers[index].PendingJumpSpeed > 0.0f
                && mover.IsGrounded())
            {
                mover.Jump(controllers[index].PendingJumpSpeed);
            }
            controllers[index].PendingJumpSpeed = 0.0f;
            mover.Move(
                Vec3d(
                    controllers[index].DesiredVelocity.X,
                    0.0f,
                    controllers[index].DesiredVelocity.Z),
                dt,
                gravity);
            controllers[index].Grounded = mover.IsGrounded();
            transforms[index].Value.Position = mover.GetPosition();
        }
    });
}

void CharacterMoverPool::EvictPartition(
    World& world,
    StoragePartitionId partition)
{
    if (!Ready(world) || !S)
        return;

    State& state = *S;
    for (uint32_t slot = 0; slot < state.Slots.size(); ++slot)
    {
        if (!state.Slots[slot].Mover)
            continue;

        const EntityId owner = state.Slots[slot].Owner;
        if (!world.IsAlive(owner)
            || world.GetEntityPartition(owner) != partition)
        {
            continue;
        }

        if (world.HasComponent<CharacterMoverLink>(owner))
            state.Commands.RemoveComponent<CharacterMoverLink>(owner);
        state.Release(slot);
    }
    state.Commands.Flush();
    state.ActivePartitions.Remove(partition);
    LastStructuralVersion = world.StructuralVersion();
}

void CharacterMoverPool::EvictAll(World& world)
{
    if (!Ready(world) || !S)
        return;

    State& state = *S;
    for (uint32_t slot = 0; slot < state.Slots.size(); ++slot)
    {
        if (!state.Slots[slot].Mover)
            continue;

        const EntityId owner = state.Slots[slot].Owner;
        if (world.IsAlive(owner)
            && world.HasComponent<CharacterMoverLink>(owner))
        {
            state.Commands.RemoveComponent<CharacterMoverLink>(owner);
        }
        state.Release(slot);
    }
    state.Commands.Flush();
    state.ActivePartitions.Clear();
    LastStructuralVersion = world.StructuralVersion();
}
