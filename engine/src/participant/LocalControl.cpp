#include <participant/LocalControl.h>

#include <controller/LookOrientation.h>
#include <ecs/World.h>

EntityId LocalControlSubjectOf(const World& world)
{
    const LocalControlSubject* held = world.TryGetResource<LocalControlSubject>();
    if (held == nullptr || !held->Value.IsValid() || !world.IsAlive(held->Value))
        return EntityId{};
    return held->Value;
}

LocalControlChange SetLocalControlSubject(World& world, EntityId entity)
{
    const EntityId previous = LocalControlSubjectOf(world);
    const EntityId current = entity.IsValid() && world.IsAlive(entity)
        ? entity
        : EntityId{};

    LocalControlChange change{
        .Previous = previous,
        .Current = current,
        .Changed = previous != current,
    };
    if (!change.Changed)
        return change;

    if (previous.IsValid() && world.IsRegistered<LocalLookControl>()
        && world.HasComponent<LocalLookControl>(previous))
    {
        world.RemoveComponent<LocalLookControl>(previous);
    }

    if (current.IsValid() && world.IsRegistered<LocalLookControl>()
        && !world.HasComponent<LocalLookControl>(current))
    {
        world.AddComponent<LocalLookControl>(current, {});
    }

    if (LocalControlSubject* held = world.TryGetResource<LocalControlSubject>())
        held->Value = current;
    else
        world.AddResource<LocalControlSubject>().Value = current;

    return change;
}

