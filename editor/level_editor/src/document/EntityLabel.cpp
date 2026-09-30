#include "document/EntityLabel.h"

#include "document/EntityNameComponent.h"

#include <ecs/World.h>

std::string EntityLabel(const World& world, EntityId entity)
{
    if (const auto* named = world.TryGet<EntityNameComponent>(entity); named != nullptr && !named->Value.Empty())
        return std::string(named->Value.View());
    return "Entity " + std::to_string(entity.Index);
}
