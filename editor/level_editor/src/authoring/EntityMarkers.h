#pragma once

#include "icons/IconId.h"

#include <ecs/EntityId.h>
#include <math/Vec.h>
#include <world/registry/RegistryId.h>

#include <cstdint>
#include <span>
#include <vector>

class EditorScene;
class IComponentSerializer;

// An entity standing in the views as an icon at its origin, picked by how near
// the pointer is to it there.
struct EditorMarker
{
    EntityId Entity;
    Vec3d World;
    IconId Icon = IconId::None;
};

// Which entities stand in the views as a marker. One carrying a component that
// declares a place icon wears it; one with nothing else to click -- no brush
// body, nothing an adapter draws -- wears the empty mark. Who wears what is
// worked out again only when the scene's structure changes.
class EntityMarkers
{
public:
    void Collect(const EditorScene& scene, std::span<const IComponentSerializer* const> drawnElsewhere,
                 std::vector<EditorMarker>& out);

private:
    void Refresh(const EditorScene& scene, std::span<const IComponentSerializer* const> drawnElsewhere);

    struct Marked
    {
        EntityId Entity;
        IconId Icon = IconId::None;
    };
    std::vector<Marked> Wearing;
    RegistryId Registry = RegistryId::Invalid();
    std::uint64_t StructuralVersion = 0;
    bool Known = false;
};
