#include <participant/LocalControl.h>

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

    // Identity only. What controller facilities switch on because of it --
    // whose look input a body takes, which camera follows -- is a game's
    // composition rule, and a game that wants one watches this value.
    if (LocalControlSubject* held = world.TryGetResource<LocalControlSubject>())
        held->Value = current;
    else
        world.AddResource<LocalControlSubject>().Value = current;

    return change;
}

