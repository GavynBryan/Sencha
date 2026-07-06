#include "BrushMeshStore.h"

#include <algorithm>
#include <utility>

BrushId BrushMeshStore::Create(BrushMesh mesh)
{
    const BrushId id{ NextId++ };
    Meshes.emplace(id.Value, std::move(mesh));
    return id;
}

void BrushMeshStore::Set(BrushId id, BrushMesh mesh)
{
    if (!id.IsValid())
        return;
    Meshes[id.Value] = std::move(mesh);
    NextId = std::max(NextId, id.Value + 1); // keep Create() ids unique after a load
}

BrushMesh* BrushMeshStore::Find(BrushId id)
{
    auto it = Meshes.find(id.Value);
    return it == Meshes.end() ? nullptr : &it->second;
}

const BrushMesh* BrushMeshStore::Find(BrushId id) const
{
    auto it = Meshes.find(id.Value);
    return it == Meshes.end() ? nullptr : &it->second;
}

void BrushMeshStore::Destroy(BrushId id)
{
    Meshes.erase(id.Value);
}

void BrushMeshStore::Clear()
{
    Meshes.clear();
    NextId = 1;
}
