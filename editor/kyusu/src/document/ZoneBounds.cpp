#include "ZoneBounds.h"

#include "EditorScene.h"

std::optional<Aabb3d> ComputeZoneBounds(const EditorScene& scene)
{
    Aabb3d bounds = Aabb3d::Empty();
    bool any = false;
    for (const EntityId entity : scene.GetAllEntities())
    {
        if (const auto entityBounds = scene.TryGetWorldBounds(entity))
        {
            bounds.ExpandToInclude(*entityBounds);
            any = true;
        }
    }
    if (!any)
        return std::nullopt;
    return bounds;
}
