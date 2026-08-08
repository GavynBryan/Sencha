#include <net/ClientPrediction.h>

#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>

#include <cstring>
#include <type_traits>

namespace
{
    // One place naming the pawn's movement state, so the intercept test, the
    // shadow lookup, and the restore cannot drift apart.
    template <typename Fn>
    auto WithPawnComponent(ComponentTypeId type, Fn&& visit)
    {
        if (type == ResolveComponentTypeId<LocalTransform>())
            return visit(std::integral_constant<int, 0>{});
        if (type == ResolveComponentTypeId<KinematicState>())
            return visit(std::integral_constant<int, 1>{});
        if (type == ResolveComponentTypeId<SupportState>())
            return visit(std::integral_constant<int, 2>{});
        if (type == ResolveComponentTypeId<CharacterMovement>())
            return visit(std::integral_constant<int, 3>{});
        if (type == ResolveComponentTypeId<JumpState>())
            return visit(std::integral_constant<int, 4>{});
        return visit(std::integral_constant<int, -1>{});
    }
}

bool ClientPrediction::Intercepts(EntityId entity, ComponentTypeId type) const
{
    if (!Enabled || !Predicts(entity))
        return false;
    return WithPawnComponent(type,
                             [](auto slot) { return decltype(slot)::value >= 0; });
}

std::span<std::byte> ClientPrediction::AuthoritativeBytes(ComponentTypeId type)
{
    const auto bytes = [](auto& value) {
        return std::span<std::byte>(reinterpret_cast<std::byte*>(&value),
                                    sizeof(value));
    };
    return WithPawnComponent(type, [&](auto slot) -> std::span<std::byte> {
        constexpr int index = decltype(slot)::value;
        if constexpr (index == 0)
            return bytes(Authoritative.Transform);
        else if constexpr (index == 1)
            return bytes(Authoritative.Motion);
        else if constexpr (index == 2)
            return bytes(Authoritative.Support);
        else if constexpr (index == 3)
            return bytes(Authoritative.Movement);
        else if constexpr (index == 4)
            return bytes(Authoritative.Jump);
        else
            return {};
    });
}

bool ClientPrediction::HasAuthoritativeState(ComponentTypeId type) const
{
    return WithPawnComponent(type, [&](auto slot) {
        constexpr int index = decltype(slot)::value;
        if constexpr (index == 0)
            return SeenAuthority.Transform;
        else if constexpr (index == 1)
            return SeenAuthority.Motion;
        else if constexpr (index == 2)
            return SeenAuthority.Support;
        else if constexpr (index == 3)
            return SeenAuthority.Movement;
        else if constexpr (index == 4)
            return SeenAuthority.Jump;
        else
            return false;
    });
}

void ClientPrediction::MarkSeen(ComponentTypeId type)
{
    WithPawnComponent(type, [&](auto slot) {
        constexpr int index = decltype(slot)::value;
        if constexpr (index == 0)
            SeenAuthority.Transform = true;
        else if constexpr (index == 1)
            SeenAuthority.Motion = true;
        else if constexpr (index == 2)
            SeenAuthority.Support = true;
        else if constexpr (index == 3)
            SeenAuthority.Movement = true;
        else if constexpr (index == 4)
            SeenAuthority.Jump = true;
        return 0;
    });
}

bool ClientPrediction::RestoreTo(World& world, const WorldComponentSchema& schema,
                                 EntityId entity) const
{
    if (!entity.IsValid() || !world.IsAlive(entity))
        return false;

    bool restored = false;
    const auto put = [&](bool seen, ComponentTypeId type, const auto& value) {
        if (!seen)
            return;
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(&value), sizeof(value));
        if (schema.SetComponentBytes(world, entity, type, bytes))
            restored = true;
    };

    put(SeenAuthority.Transform, ResolveComponentTypeId<LocalTransform>(),
        Authoritative.Transform);
    put(SeenAuthority.Motion, ResolveComponentTypeId<KinematicState>(),
        Authoritative.Motion);
    put(SeenAuthority.Support, ResolveComponentTypeId<SupportState>(),
        Authoritative.Support);
    put(SeenAuthority.Movement, ResolveComponentTypeId<CharacterMovement>(),
        Authoritative.Movement);
    put(SeenAuthority.Jump, ResolveComponentTypeId<JumpState>(),
        Authoritative.Jump);
    return restored;
}

void ClientPrediction::SetPredicted(EntityId entity)
{
    if (entity == Subject)
        return;
    // A new pawn means everything kept for the old one is about somebody else.
    Reset();
    Subject = entity;
}

void ClientPrediction::NoteReconcile(std::uint32_t ticksReplayed,
                                     float resetMeters, bool snapped)
{
    ++Reconciled;
    LastReplayed = ticksReplayed;
    LastReset = resetMeters;
    if (snapped)
        ++SnapCount;
}

void ClientPrediction::Reset()
{
    Ring.Clear();
    Authoritative = Shadow{};
    SeenAuthority = Seen{};
    Subject = EntityId{};
    LastReset = 0.0f;
    LastReplayed = 0;
    Reconciled = 0;
    SnapCount = 0;
}
