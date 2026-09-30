#include "ArrayModifier.h"

Vec3d ResolveArrayStep(const ArrayModifier& array, const Aabb3d& inputBounds)
{
    Vec3d step;
    if (array.Placement == ArrayPlacement::ConstantOffset)
    {
        step = array.Offset;
    }
    else
    {
        const float extent = inputBounds.IsValid()
            ? LocalAxisComponent(inputBounds.Max - inputBounds.Min, array.Axis)
            : 0.0f;
        step = LocalAxisVector(array.Axis) * (extent + array.Spacing);
    }
    return array.Reverse ? -step : step;
}
