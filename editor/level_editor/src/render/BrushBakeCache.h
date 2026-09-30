#pragma once

#include "brush/BrushEvaluation.h"
#include "brush/BrushId.h"

#include <core/assets/AssetRef.h>
#include <math/geometry/3d/Aabb3d.h>
#include <render/Material.h>                    // MaterialHandle
#include <render/static_mesh/StaticMeshHandle.h>
#include <world/registry/RegistryId.h>

#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

struct MeshGeometry;

//=============================================================================
// BrushBakeCache — GPU residency for evaluated brush meshes, one baked static
// mesh per distinct mesh content.
//
// A brush's evaluation is a few distinct meshes and many placements. The
// viewport draws the placements as instances of the baked meshes, so this
// cache owns exactly the mesh-sized work: tessellating a distinct mesh in its
// own frame, baking it, uploading it, leasing its materials. It is keyed by
// mesh content (BrushMeshSignature), never by mesh pointer or record
// revision: a Count or Spacing edit re-evaluates the record and mints nothing
// new, so it bakes nothing; a mirror plane edit changes one reflected mesh, so
// it bakes one.
//
// Residency is tied to existence, not visibility: a baked mesh lives until its
// brush leaves its document's store, its document closes, or its content is no
// longer part of the brush's evaluation. Hiding a brush keeps it resident.
//
// GPU calls arrive through the injected functions so the reconcile rules can
// be tested with counting fakes; the cache itself never touches Vulkan.
//=============================================================================

struct BrushBakedMesh
{
    std::uint64_t               Signature = 0;   // BrushMeshSignature of the baked content
    StaticMeshHandle            Handle;
    std::vector<MaterialHandle> SlotMaterials;   // index = StaticMeshSection::MaterialSlot
    Aabb3d                      LocalBounds = Aabb3d::Empty(); // MeshGeometry::LocalBounds, brush-local
    // Signature folded with the resolved default material: what the baked
    // triangles and their materials are, for the lightmap-stale digest.
    std::uint64_t               ContentHash = 0;
};

struct BrushBakedRecord
{
    std::uint64_t               DefaultMaterialHash = 0;
    std::vector<BrushBakedMesh> Meshes; // aligned with BrushEvaluated::Meshes
};

class BrushBakeCache
{
public:
    struct Gpu
    {
        std::function<StaticMeshHandle(const MeshGeometry&)> Bake;    // create + upload
        std::function<void(StaticMeshHandle)>                Destroy;
        std::function<MaterialHandle(const AssetRef&)>       Lease;
        std::function<void(MaterialHandle)>                  Release;
    };

    explicit BrushBakeCache(Gpu gpu);
    ~BrushBakeCache();

    BrushBakeCache(const BrushBakeCache&) = delete;
    BrushBakeCache& operator=(const BrushBakeCache&) = delete;

    // The record's baked meshes, aligned with `evaluated.Meshes`. Reconciles
    // when the evaluation's signature list or the level default differs from
    // what is cached: meshes whose signature is already baked keep their
    // handles, new signatures bake, retired ones are destroyed. Otherwise a
    // lookup and a signature compare.
    const BrushBakedRecord& Ensure(RegistryId registry, BrushId brush,
                                   const BrushEvaluated& evaluated,
                                   const AssetRef& levelDefault);

    // Drops records whose document is not open or whose brush no longer exists
    // in its store. Never consults visibility.
    void Sweep(std::span<const RegistryId> openRegistries,
               const std::function<bool(RegistryId, BrushId)>& exists);

    void Clear();

    [[nodiscard]] std::size_t BakeCount() const { return Bakes; }     // meshes baked so far
    [[nodiscard]] std::size_t DestroyCount() const { return Destroys; }
    [[nodiscard]] std::size_t RecordCount() const { return Records.size(); }

private:
    struct Key
    {
        RegistryId Registry;
        BrushId    Brush;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash
    {
        std::size_t operator()(const Key& key) const;
    };

    void Reconcile(BrushBakedRecord& record, const BrushEvaluated& evaluated,
                   const AssetRef& levelDefault, std::uint64_t defaultHash);
    BrushBakedMesh BakeOne(const BrushEvaluatedMesh& mesh, const AssetRef& levelDefault,
                           std::uint64_t defaultHash);
    void ReleaseMesh(const BrushBakedMesh& baked);

    Gpu Callbacks;
    std::unordered_map<Key, BrushBakedRecord, KeyHash> Records;
    std::size_t Bakes = 0;
    std::size_t Destroys = 0;
};
