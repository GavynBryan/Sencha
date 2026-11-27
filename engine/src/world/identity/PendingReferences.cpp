#include <world/identity/PendingReferences.h>

#include <ecs/World.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

void PendingReferences::Await(EntityId owner, ComponentTypeId type, std::uint32_t fieldOffset,
                              PersistentEntityId target)
{
    const auto same = [&](const Reference& waiting) {
        return waiting.Owner == owner && waiting.Type == type && waiting.FieldOffset == fieldOffset;
    };
    std::erase_if(Waiting, same);
    Waiting.push_back(Reference{ .Owner = owner, .Type = type, .FieldOffset = fieldOffset, .Target = target });
}

bool PendingReferences::Awaits(EntityId owner, ComponentTypeId type) const
{
    return std::ranges::any_of(Waiting, [&](const Reference& waiting) {
        return waiting.Owner == owner && waiting.Type == type;
    });
}

void PendingReferences::Forget(EntityId owner, ComponentTypeId type)
{
    std::erase_if(Waiting, [&](const Reference& waiting) { return waiting.Owner == owner && waiting.Type == type; });
}

void PendingReferences::Arrived(World& world, PersistentEntityId id, EntityId entity)
{
    std::erase_if(Waiting, [&](const Reference& waiting) {
        if (!world.IsAlive(waiting.Owner))
            return true;
        if (waiting.Target != id)
            return false;
        const ComponentId component = world.GetComponentIdByType(waiting.Type);
        void* row = component != InvalidComponentId ? world.GetComponentRaw(waiting.Owner, component) : nullptr;
        if (row != nullptr)
            std::memcpy(static_cast<std::byte*>(row) + waiting.FieldOffset, &entity, sizeof(EntityId));
        return true;
    });
}

void ForgetPendingReferences(World& world, EntityId owner, ComponentTypeId type)
{
    if (PendingReferences* pending = world.TryGetResource<PendingReferences>())
        pending->Forget(owner, type);
}
