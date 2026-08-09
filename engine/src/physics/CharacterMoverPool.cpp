#include <physics/CharacterMoverPool.h>

#include <optional>
#include <vector>

#include <ecs/CommandBuffer.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <physics/CharacterMover.h>
#include <physics/PhysicsWorld.h>
#include <movement/MovementComponents.h>
#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

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

// The motor's support classification and the movement component's are the same
// three cases named in each layer's own vocabulary; neither depends on the
// other's header.
SupportKind ToSupportKind(CharacterSupportKind kind)
{
    switch (kind)
    {
    case CharacterSupportKind::Stable: return SupportKind::Stable;
    case CharacterSupportKind::Steep:  return SupportKind::Steep;
    case CharacterSupportKind::None:   break;
    }
    return SupportKind::None;
}
} // namespace

struct CharacterMoverPool::State
{
    explicit State(World& world)
        : Commands(world)
        , PendingQuery(world)
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
    // Controllers still awaiting a mover. Without<CharacterMoverLink> is the
    // archetype-level form of the per-entity presence test, so an already-bound
    // controller costs nothing to skip.
    Query<Read<CharacterController>, Without<CharacterMoverLink>> PendingQuery;
    // The motor consumes the composed request and publishes the facts the
    // movement pipeline reads next tick; the authored controller shape is only
    // needed when a mover is created.
    //
    // Built on the first drive, not with the pool: naming a component the
    // world never registered is an assert, and a physics-only world binds
    // movers without ever driving them.
    using CharacterDriveQuery = Query<
        Write<LocalTransform>,
        Read<CharacterMoverLink>,
        Write<MotionRequest>,
        Write<KinematicState>,
        Write<SupportState>>;
    std::optional<CharacterDriveQuery> DriveQuery;
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

bool CharacterMoverPool::ReadyToDrive(const World& world) const
{
    // Binding a mover needs only the authored capsule; driving one needs the
    // movement components it reads and writes. A world that has characters but
    // no movement pipeline still binds them, which is what the residency tests
    // and a physics-only host rely on.
    return Ready(world)
        && world.IsRegistered<MotionRequest>()
        && world.IsRegistered<KinematicState>()
        && world.IsRegistered<SupportState>();
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

    // Movers exist only for controllers in the active set, so only that set's
    // structural churn can change what needs a mover.
    State& state = EnsureState(world);
    if (world.StructuralVersion(partitions) == LastStructuralVersion
        && state.ActivePartitions == partitions)
    {
        return;
    }

    ++ReconcileCount;
    const World& readOnly = world;

    state.PendingQuery.ForEachChunkIn(partitions, [&](auto& view)
    {
        const auto controllers = view.template Read<CharacterController>();
        for (uint32_t index = 0; index < view.Count(); ++index)
        {
            const EntityId entity = view.Entity(index);
            const uint32_t slot = state.Allocate(entity);
            CharacterMoverConfig config;
            config.Radius = controllers[index].Radius;
            config.Height = controllers[index].Height;
            config.SlopeLimitDegrees = controllers[index].SlopeLimitDegrees;
            config.StepHeight = controllers[index].StepHeight;
            config.GroundSnapDistance = controllers[index].GroundSnapDistance;
            config.SkinWidth = controllers[index].SkinWidth;
            state.Slots[slot].Mover.emplace(
                *Simulation,
                config,
                ReadPosition(readOnly, entity));
            state.Commands.AddComponent<CharacterMoverLink>(
                entity,
                CharacterMoverLink{ slot });
        }
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
    state.ActivePartitions = partitions;
    LastStructuralVersion = world.StructuralVersion(partitions);
}

void CharacterMoverPool::Sweep(
    CharacterMover& mover,
    const MotionRequest& request,
    float dt,
    const Vec3d& gravity,
    KinematicState& kinematics,
    SupportState& support,
    LocalTransform& transform)
{
    CharacterMoveRequest move;
    move.Velocity = request.Velocity;
    move.UpAxis = request.UpAxis;
    move.GravityScale = request.GravityScale;
    move.Gravity = gravity;
    move.DeltaSeconds = dt;

    const CharacterMoveResult result = mover.Move(move);

    // The achieved velocity is what locomotion reads next tick, so a character
    // that hit a wall does not keep the velocity it asked for.
    kinematics.Velocity = result.Velocity;
    support.Kind = ToSupportKind(result.Support.Kind);
    support.Surface = result.Support.Surface;
    support.ContactPoint = result.Support.ContactPoint;
    support.Normal = result.Support.Normal;
    support.SurfaceVelocity = result.Support.Velocity;

    transform.Value.Position = result.Position;
}

void CharacterMoverPool::Drive(
    World& world,
    const StoragePartitionSet& partitions,
    float dt,
    const Vec3d& gravity)
{
    if (!ReadyToDrive(world) || !S)
        return;

    State& state = *S;
    if (!state.DriveQuery)
        state.DriveQuery.emplace(world);

    state.DriveQuery->ForEachChunkIn(partitions, [&](auto& view)
    {
        auto transforms = view.template Write<LocalTransform>();
        const auto links = view.template Read<CharacterMoverLink>();
        auto requests = view.template Write<MotionRequest>();
        auto kinematics = view.template Write<KinematicState>();
        auto supports = view.template Write<SupportState>();

        for (uint32_t index = 0; index < view.Count(); ++index)
        {
            Sweep(*state.Slots[links[index].MoverSlot].Mover, requests[index],
                  dt, gravity, kinematics[index], supports[index],
                  transforms[index]);
        }
    });
}

bool CharacterMoverPool::Step(World& world, EntityId entity,
                              const MotionRequest& request, float dt,
                              const Vec3d& gravity)
{
    if (!Ready(world) || !S)
        return false;

    const CharacterMoverLink* link = world.TryGet<CharacterMoverLink>(entity);
    if (link == nullptr || link->MoverSlot >= S->Slots.size())
        return false;
    State::Slot& slot = S->Slots[link->MoverSlot];
    if (slot.Owner != entity || !slot.Mover)
        return false;

    KinematicState* kinematics = world.TryGet<KinematicState>(entity);
    SupportState* support = world.TryGet<SupportState>(entity);
    LocalTransform* transform = world.TryGet<LocalTransform>(entity);
    if (kinematics == nullptr || support == nullptr || transform == nullptr)
        return false;

    Sweep(*slot.Mover, request, dt, gravity, *kinematics, *support, *transform);
    return true;
}

bool CharacterMoverPool::RestorePosition(World& world, EntityId entity,
                                         const Vec3d& position)
{
    if (!Ready(world) || !S)
        return false;
    const CharacterMoverLink* link = world.TryGet<CharacterMoverLink>(entity);
    LocalTransform* transform = world.TryGet<LocalTransform>(entity);
    if (link == nullptr || transform == nullptr || link->MoverSlot >= S->Slots.size())
        return false;
    State::Slot& slot = S->Slots[link->MoverSlot];
    if (slot.Owner != entity || !slot.Mover)
        return false;

    // No history snap: the pawn is being returned to where the authority says
    // it already was, and the replay that follows moves it on from there.
    slot.Mover->SetPosition(position);
    transform->Value.Position = position;
    return true;
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
    LastStructuralVersion = world.StructuralVersion(state.ActivePartitions);
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
    LastStructuralVersion = world.StructuralVersion(state.ActivePartitions);
}

bool CharacterMoverPool::SetPosition(World& world, EntityId entity,
                                     const Vec3d& position)
{
    if (!Ready(world) || !S)
        return false;
    const CharacterMoverLink* link = world.TryGet<CharacterMoverLink>(entity);
    LocalTransform* transform = world.TryGet<LocalTransform>(entity);
    if (link == nullptr || transform == nullptr || link->MoverSlot >= S->Slots.size())
        return false;
    State::Slot& slot = S->Slots[link->MoverSlot];
    if (slot.Owner != entity || !slot.Mover)
        return false;
    slot.Mover->SetPosition(position);
    transform->Value.Position = position;
    // This is a teleport, not motion. Rendering must not blend the character
    // across the gap it just skipped.
    RequestTransformHistorySnap(world, entity);
    return true;
}
