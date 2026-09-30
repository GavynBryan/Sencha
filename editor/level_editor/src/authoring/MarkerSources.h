#pragma once

#include "EditorComponentAdapter.h"
#include "document/EditorScene.h"

// The marker source for entities carrying `Component`, wearing `icon`.
template <typename Component>
[[nodiscard]] MarkerSource MarkEntitiesWith(IconId icon)
{
    return { icon, [](const EditorScene& scene, IconId mark, std::vector<EditorMarker>& out) {
                scene.GetRegistry().Components.ForEachComponent<Component>([&](EntityId entity, const Component&) {
                    if (!scene.IsEntityEffectivelyVisible(entity))
                        return;
                    if (const Transform3f* transform = scene.TryGetWorldTransform(entity))
                        out.push_back({ .Entity = entity, .World = transform->Position, .Icon = mark });
                });
            } };
}
