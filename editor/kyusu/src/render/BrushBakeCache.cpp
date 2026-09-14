#include "BrushBakeCache.h"

#include "brush/BrushWorkCounters.h"
#include "document/BrushCookInput.h"

#include <assets/cook/BrushGeometryCook.h> // CollectMaterialOrder, BakeBrushFacesToStaticMesh
#include <assets/static_mesh/MeshGeometry.h>
#include <core/hash/Fnv1a.h>

#include <algorithm>
#include <utility>

namespace
{
    std::uint64_t HashMaterialPath(const AssetRef& material)
    {
        std::uint64_t h = kFnv1aOffsetBasis;
        HashFnv1aBytes(h, material.Path.data(), material.Path.size());
        return h;
    }

    bool SignaturesMatch(const BrushBakedRecord& record, const BrushEvaluated& evaluated)
    {
        if (record.Meshes.size() != evaluated.Meshes.size())
            return false;
        for (std::size_t i = 0; i < record.Meshes.size(); ++i)
            if (record.Meshes[i].Signature != evaluated.Meshes[i].Signature)
                return false;
        return true;
    }
}

std::size_t BrushBakeCache::KeyHash::operator()(const Key& key) const
{
    std::uint64_t h = kFnv1aOffsetBasis;
    HashFnv1aValue(h, key.Registry.Index);
    HashFnv1aValue(h, key.Registry.Generation);
    HashFnv1aValue(h, key.Brush.Value);
    return static_cast<std::size_t>(h);
}

BrushBakeCache::BrushBakeCache(Gpu gpu)
    : Callbacks(std::move(gpu))
{
}

BrushBakeCache::~BrushBakeCache()
{
    Clear();
}

const BrushBakedRecord& BrushBakeCache::Ensure(RegistryId registry, BrushId brush,
                                               const BrushEvaluated& evaluated,
                                               const AssetRef& levelDefault)
{
    BrushBakedRecord& record = Records[Key{ registry, brush }];
    const std::uint64_t defaultHash = HashMaterialPath(levelDefault);
    if (record.DefaultMaterialHash != defaultHash || !SignaturesMatch(record, evaluated))
        Reconcile(record, evaluated, levelDefault, defaultHash);
    return record;
}

void BrushBakeCache::Reconcile(BrushBakedRecord& record, const BrushEvaluated& evaluated,
                               const AssetRef& levelDefault, std::uint64_t defaultHash)
{
    // A default-material change re-resolves every empty face, so nothing baked
    // under the old default can be kept.
    std::vector<BrushBakedMesh> retired;
    if (record.DefaultMaterialHash == defaultHash)
        retired = std::move(record.Meshes);
    else
        for (const BrushBakedMesh& baked : record.Meshes)
            ReleaseMesh(baked);
    record.Meshes.clear();
    record.DefaultMaterialHash = defaultHash;

    // Bake before destroying so a retired handle is never reused by a bake in
    // the same reconcile.
    record.Meshes.reserve(evaluated.Meshes.size());
    for (const BrushEvaluatedMesh& mesh : evaluated.Meshes)
    {
        const auto kept = std::find_if(retired.begin(), retired.end(),
            [&](const BrushBakedMesh& baked) { return baked.Signature == mesh.Signature; });
        if (kept != retired.end())
        {
            record.Meshes.push_back(std::move(*kept));
            retired.erase(kept);
            continue;
        }
        record.Meshes.push_back(BakeOne(mesh, levelDefault, defaultHash));
    }
    for (const BrushBakedMesh& baked : retired)
        ReleaseMesh(baked);
}

BrushBakedMesh BrushBakeCache::BakeOne(const BrushEvaluatedMesh& mesh,
                                       const AssetRef& levelDefault,
                                       std::uint64_t defaultHash)
{
    BrushBakedMesh baked;
    baked.Signature = mesh.Signature;
    baked.ContentHash = mesh.Signature ^ (defaultHash * 0x9E3779B97F4A7C15ull);
    if (mesh.Mesh == nullptr)
        return baked;

    const CookBrushGeometry geometry =
        CollectBrushGeometry(*mesh.Mesh, Transform3f::Identity(), levelDefault);
    if (geometry.Faces.empty())
        return baked;

    const std::vector<AssetRef> order = CollectMaterialOrder(geometry.Faces);
    MeshGeometry meshGeometry;
    if (!BakeBrushFacesToStaticMesh(geometry.Faces, order, meshGeometry))
        return baked;

    baked.Handle = Callbacks.Bake(meshGeometry);
    ++Bakes;
    ++BrushWorkCounters::Frame().Bakes;
    if (!baked.Handle.IsValid())
        return baked;
    baked.LocalBounds = meshGeometry.LocalBounds;
    baked.SlotMaterials.reserve(order.size());
    for (const AssetRef& ref : order)
        baked.SlotMaterials.push_back(Callbacks.Lease(ref));
    return baked;
}

void BrushBakeCache::ReleaseMesh(const BrushBakedMesh& baked)
{
    for (const MaterialHandle material : baked.SlotMaterials)
        if (material.IsValid())
            Callbacks.Release(material);
    if (baked.Handle.IsValid())
    {
        Callbacks.Destroy(baked.Handle);
        ++Destroys;
    }
}

void BrushBakeCache::Sweep(std::span<const RegistryId> openRegistries,
                           const std::function<bool(RegistryId, BrushId)>& exists)
{
    for (auto it = Records.begin(); it != Records.end();)
    {
        const Key& key = it->first;
        const bool open = std::find(openRegistries.begin(), openRegistries.end(), key.Registry)
            != openRegistries.end();
        if (open && exists(key.Registry, key.Brush))
        {
            ++it;
            continue;
        }
        for (const BrushBakedMesh& baked : it->second.Meshes)
            ReleaseMesh(baked);
        it = Records.erase(it);
    }
}

void BrushBakeCache::Clear()
{
    for (auto& [key, record] : Records)
        for (const BrushBakedMesh& baked : record.Meshes)
            ReleaseMesh(baked);
    Records.clear();
}
