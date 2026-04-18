#pragma once

#include <core/service/IService.h>
#include <entity/EntityId.h>
#include <registry/Registry.h>
#include <transform/TransformHierarchyService.h>
#include <transform/TransformPropagationOrderService.h>
#include <transform/TransformStore.h>

// Deprecated compatibility shim. New code should use Registry plus explicit
// transform services or a dimension-specific registry service.
template <typename TTransform>
class [[deprecated("Use Registry and explicit frame views instead.")]] World : public IService
{
public:
    World()
        : Storage{ RegistryId::Global(), RegistryKind::Global }
        , Entities(Storage.Entities)
        , Components(Storage.Components)
        , Transforms(PropagationOrder)
    {
    }

    Registry Storage;
    EntityRegistry& Entities;
    ComponentRegistry& Components;

    TransformHierarchyService Hierarchy;
    TransformPropagationOrderService PropagationOrder;
    TransformStore<TTransform> Transforms;

    void DestroySubtree(EntityId root)
    {
        Entities.DestroySubtree(root, Hierarchy);
    }
};
