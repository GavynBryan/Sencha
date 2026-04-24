#pragma once

#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <world/transform/TransformStore.h>

//=============================================================================
// TransformSpace<TTransform>
//
// Migration-only compatibility shell for older transform tests. Production ECS
// storage is LocalTransform + WorldTransform + Parent in World.
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
