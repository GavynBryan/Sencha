#include "MeshElementKindTraits.h"

namespace
{
// The one authority for element-mode order + mappings. Array index == enum value.
const std::array<MeshElementKindTraits, MeshElementKindCount> kTraits = {
    MeshElementKindTraits{ MeshElementKind::Object, SelectableKind::Entity, MeshElementKind::Vertex, "Object" },
    MeshElementKindTraits{ MeshElementKind::Vertex, SelectableKind::Vertex, MeshElementKind::Edge,   "Vertex" },
    MeshElementKindTraits{ MeshElementKind::Edge,   SelectableKind::Edge,   MeshElementKind::Face,   "Edge"   },
    MeshElementKindTraits{ MeshElementKind::Face,   SelectableKind::Face,   MeshElementKind::Object, "Face"   },
};

const std::array<MeshElementKind, MeshElementKindCount> kOrder = {
    MeshElementKind::Object,
    MeshElementKind::Vertex,
    MeshElementKind::Edge,
    MeshElementKind::Face,
};
}

const MeshElementKindTraits& Traits(MeshElementKind kind)
{
    return kTraits[static_cast<std::size_t>(kind)];
}

const std::array<MeshElementKind, MeshElementKindCount>& AllMeshElementKinds()
{
    return kOrder;
}

MeshElementKind MeshElementKindForSelectable(SelectableKind selectable)
{
    // Linear scan (4 rows) rather than a static_cast, so the two enums may diverge
    // safely later without a silent mismatch.
    for (const MeshElementKindTraits& traits : kTraits)
        if (traits.Selectable == selectable)
            return traits.Kind;
    return MeshElementKind::Object;
}
