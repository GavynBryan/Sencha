#pragma once

#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>

//=============================================================================
// TransformSpace<TTransform>
//
// Self-contained transform domain: entity-indexed component storage, hierarchy,
// and propagation cache. Transform lifetime is explicit Add/Remove on
// `Transforms`; no transform RAII handles are involved.
//=============================================================================
template <typename TTransform>
class TransformSpace
{
public:
    TransformSpace()
        : Transforms(PropagationOrder)
    {
    }

    TransformSpace(const TransformSpace&) = delete;
    TransformSpace& operator=(const TransformSpace&) = delete;
    TransformSpace(TransformSpace&&) = delete;
    TransformSpace& operator=(TransformSpace&&) = delete;

    TransformHierarchyService Hierarchy;
    TransformPropagationOrderService PropagationOrder;
    TransformStore<TTransform> Transforms;
};

using TransformSpace2d = TransformSpace<Transform2f>;
using TransformSpace3d = TransformSpace<Transform3f>;
