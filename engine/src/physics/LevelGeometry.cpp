#include <physics/LevelGeometry.h>

#include <attributes/AttributeSet.h>
#include <ecs/World.h>
#include <physics/components/CharacterMoverLink.h>
#include <physics/components/RigidBody.h>

bool IsLevelGeometry(const World& world, EntityId entity)
{
    if (!world.IsAlive(entity))
        return true;
    if (world.HasComponent<AttributeSet>(entity)
        || (world.IsRegistered<CharacterMoverLink>() && world.HasComponent<CharacterMoverLink>(entity)))
        return false;
    const RigidBody* body = world.IsRegistered<RigidBody>() ? world.TryGet<RigidBody>(entity) : nullptr;
    return body == nullptr || body->Motion == BodyMotion::Static;
}
