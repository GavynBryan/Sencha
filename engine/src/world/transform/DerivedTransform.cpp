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

Transform3f ComposeWorldTransform(const World& world, EntityId entity)
{
    Transform3f composed = Transform3f::Identity();
    if (!world.IsRegistered<LocalTransform>())
        return composed;
    // A chain longer than the live entities is a cycle, so damaged parentage ends.
    for (std::size_t remaining = world.EntityCount() + 1; entity.IsValid() && remaining > 0; --remaining)
    {
        if (const LocalTransform* local = world.TryGet<LocalTransform>(entity))
            composed = local->Value * composed;
        const Parent* parent = world.TryGet<Parent>(entity);
        entity = parent != nullptr && world.IsAlive(parent->Entity) ? parent->Entity : EntityId{};
    }
    return composed;
}
