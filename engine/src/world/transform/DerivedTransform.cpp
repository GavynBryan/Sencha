#include <world/transform/DerivedTransform.h>

#include <ecs/World.h>
#include <world/transform/TransformComponents.h>

void SeedDerivedWorldTransform(World& world, EntityId entity)
{
    if (!world.IsRegistered<LocalTransform>() || !world.IsRegistered<WorldTransform>())
        return;

    const LocalTransform* local = world.TryGet<LocalTransform>(entity);
    if (local == nullptr)
        return;

    const Transform3f seeded = local->Value;
    // Already there: the caller is re-seeding an entity whose local transform
    // changed before propagation ran. Writing it keeps the pair consistent.
    if (WorldTransform* existing = world.TryGet<WorldTransform>(entity))
    {
        existing->Value = seeded;
        return;
    }

    // Built at its final signature, so the column exists and this writes in
    // place; otherwise the column has to be added.
    if (!world.InitializeComponent<WorldTransform>(entity, WorldTransform{ seeded }))
        world.AddComponent<WorldTransform>(entity, WorldTransform{ seeded });
}
