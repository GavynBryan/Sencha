#include "PortalGeometry.h"

int DominantPortalAxis(const Aabb3d& worldBounds)
{
    const Vec3d extent = worldBounds.Extent();
    int axis = 0;
    for (int i = 1; i < 3; ++i)
        if (extent[i] < extent[axis])
            axis = i;
    return axis;
}
