#include "authoring/EntityMarkers.h"

#include "document/DocumentSerialization.h"
#include "document/EditorScene.h"
#include "icons/IconNames.h"

#include <world/serialization/ComponentSerializerRegistry.h>

#include <algorithm>

void EntityMarkers::Collect(const EditorScene& scene, std::span<const IComponentSerializer* const> drawnElsewhere,
                            std::vector<EditorMarker>& out)
{
    Refresh(scene, drawnElsewhere);
    for (const Marked& marked : Wearing)
    {
        if (!scene.IsEntityEffectivelyVisible(marked.Entity))
            continue;
        if (const Transform3f* transform = scene.TryGetWorldTransform(marked.Entity))
            out.push_back({ .Entity = marked.Entity, .World = transform->Position, .Icon = marked.Icon });
    }
}

void EntityMarkers::Refresh(const EditorScene& scene, std::span<const IComponentSerializer* const> drawnElsewhere)
{
    const ::Registry& registry = scene.GetRegistry();
    const std::uint64_t version = registry.Components.StructuralVersion();
    if (Known && registry.Id == Registry && version == StructuralVersion)
        return;
    Known = true;
    Registry = registry.Id;
    StructuralVersion = version;

    struct Declared
    {
        const IComponentSerializer* Serializer;
        IconId Icon;
    };
    std::vector<Declared> declared;
    for (const auto& serializer : EditorSceneSerializers().Entries())
        if (const IconId icon = IconByName(serializer->Placement().Icon); icon != IconId::None)
            declared.push_back({ serializer.get(), icon });

    Wearing.clear();
    for (const EntityId entity : scene.GetAllEntities())
    {
        const auto carries = [&](const IComponentSerializer* serializer) {
            return serializer->HasComponent(entity, registry);
        };
        const auto own = std::ranges::find_if(declared, carries, &Declared::Serializer);
        if (own != declared.end())
            Wearing.push_back({ entity, own->Icon });
        else if (!scene.HasBrushBody(entity) && std::ranges::none_of(drawnElsewhere, carries))
            Wearing.push_back({ entity, IconId::Empty });
    }
}
