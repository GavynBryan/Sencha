#pragma once

#include "BrushId.h"
#include "BrushMesh.h"

#include <cstdint>
#include <unordered_map>

//=============================================================================
// BrushMeshStore — owns the heavy BrushMesh data, keyed by BrushId. A resource
// on the editor's LevelDocument (not an archetype component), so BrushComponent
// stays trivially copyable. Serialized as a sidecar alongside the scene (§5).
// (docs/plans/sencha-level-editor/03-brush-representation.md §2.2)
//=============================================================================
class BrushMeshStore
{
public:
    [[nodiscard]] BrushId Create(BrushMesh mesh);

    // Insert/replace a mesh at a specific id (used by load to preserve ids).
    void Set(BrushId id, BrushMesh mesh);

    [[nodiscard]] BrushMesh*       Find(BrushId id);
    [[nodiscard]] const BrushMesh* Find(BrushId id) const;

    void Destroy(BrushId id);
    void Clear();

    [[nodiscard]] std::size_t Count() const { return Meshes.size(); }

    // Iteration for serialization (id → mesh).
    [[nodiscard]] const std::unordered_map<std::uint32_t, BrushMesh>& All() const { return Meshes; }

private:
    std::unordered_map<std::uint32_t, BrushMesh> Meshes;
    std::uint32_t NextId = 1; // 0 is the invalid BrushId
};
