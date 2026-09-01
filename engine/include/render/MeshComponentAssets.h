#pragma once

class MaterialSetCache;
class SkinnedMeshCache;
class StaticMeshCache;

//=============================================================================
// Where the drawn-mesh components' meshes and materials live, so their
// lifecycle hooks can hold what they name. A host points these at its caches;
// null in a world composed without them, where the hooks then do nothing.
//=============================================================================

struct StaticMeshComponentAssets
{
    StaticMeshComponentAssets() = default;
    StaticMeshComponentAssets(StaticMeshCache* meshes, MaterialSetCache* materialSets)
        : Meshes(meshes)
        , MaterialSets(materialSets)
    {
    }

    StaticMeshCache* Meshes = nullptr;
    MaterialSetCache* MaterialSets = nullptr;
};

struct SkinnedMeshComponentAssets
{
    SkinnedMeshComponentAssets() = default;
    SkinnedMeshComponentAssets(SkinnedMeshCache* meshes, MaterialSetCache* materialSets)
        : Meshes(meshes)
        , MaterialSets(materialSets)
    {
    }

    SkinnedMeshCache* Meshes = nullptr;
    MaterialSetCache* MaterialSets = nullptr;
};
