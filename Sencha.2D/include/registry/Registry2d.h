#pragma once

#include <core/service/IService.h>
#include <math/geometry/2d/Transform2d.h>
#include <physics/PhysicsDomain2D.h>
#include <physics/RigidBody2D.h>
#include <registry/Registry.h>
#include <sprite/SpriteStore.h>
#include <transform/TransformHierarchyService.h>
#include <transform/TransformPropagationOrderService.h>
#include <transform/TransformStore.h>

class Registry2d : public IService
{
public:
    explicit Registry2d(const PhysicsConfig2D& physicsConfig = {})
        : Storage{ RegistryId::Global(), RegistryKind::Global }
        , Entities(Storage.Entities)
        , Components(Storage.Components)
        , Transforms(Storage.Components.Register<TransformStore<Transform2f>>(PropagationOrder))
        , Physics(physicsConfig)
        , Bodies(Storage.Components.Register<RigidBodyStore>(Physics))
        , Sprites(Storage.Components.Register<SpriteStore>())
    {
    }

    Registry Storage;
    EntityRegistry& Entities;
    ComponentRegistry& Components;

    TransformHierarchyService Hierarchy;
    TransformPropagationOrderService PropagationOrder;
    TransformStore<Transform2f>& Transforms;

    PhysicsDomain2D Physics;
    RigidBodyStore& Bodies;
    SpriteStore& Sprites;
};
