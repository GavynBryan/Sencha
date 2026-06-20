#pragma once

#include <core/assets/AssetCache.h>
#include <core/handle/Handle.h>
#include <render/Material.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Handle to an ordered, immutable set of materials owned by MaterialSetCache.
// One of the engine's unified Handle<Tag> types. Transient: scene data persists
// the material paths, never this handle (the StaticMeshHandle / MaterialHandle
// rule).
using MaterialSetHandle = Handle<struct MaterialSetHandleTag>;

class MaterialCache;

struct MaterialSetEntry
{
    // Slot order: Materials[i] is the material for sections whose MaterialSlot
    // is i. The set retains a reference to each member for its lifetime, so a
    // resident set keeps its materials alive without the component tracking them
    // (the trivially-copyable component holds only the set handle).
    std::vector<MaterialHandle> Materials;
    uint32_t    Generation = 0;
    uint32_t    RefCount = 0;
    std::string PathKey; // synthesized content key; identical sets dedup
    bool        Alive = false;
};

//=============================================================================
// MaterialSetCache
//
// Ref-counted, content-deduplicated store of per-mesh material lists. It exists
// because a StaticMeshComponent must bind one material per mesh section yet stay
// trivially copyable (archetype rows are memcpy'd): the component holds an
// 8-byte MaterialSetHandle, the variable-length array lives here. Instance level
// by design (not baked into the mesh asset) so the same mesh can be placed and
// reskinned independently, which instancing, array/mirror modifiers, and reused
// baked tiles all depend on.
//
// Dedup keys on the ordered member handles, so two placements that resolve to
// the same materials share one entry. The set owns a reference to each member
// material (released when the set's refcount hits zero), mirroring how
// MaterialCache owns the textures a material points at.
//=============================================================================
class MaterialSetCache final
    : public AssetCache<MaterialSetCache, MaterialSetHandle, MaterialSetEntry>
{
public:
    // `materials` is the MaterialCache whose entries this set retains; null
    // disables member retain/release (test or ownership-free contexts).
    explicit MaterialSetCache(MaterialCache* materials = nullptr);
    ~MaterialSetCache() override;

    MaterialSetCache(const MaterialSetCache&) = delete;
    MaterialSetCache& operator=(const MaterialSetCache&) = delete;
    MaterialSetCache(MaterialSetCache&&) = delete;
    MaterialSetCache& operator=(MaterialSetCache&&) = delete;

    // Returns the handle for this exact ordered material list, allocating and
    // retaining members on first sight and ref-counting an existing match.
    [[nodiscard]] MaterialSetHandle Acquire(std::span<const MaterialHandle> materials);

    [[nodiscard]] const std::vector<MaterialHandle>* Get(MaterialSetHandle handle) const;

private:
    friend class AssetCache<MaterialSetCache, MaterialSetHandle, MaterialSetEntry>;

    // The content path is the only creation route; there is no byte source to
    // load a set from, so the base's path-keyed Acquire(path) is unused.
    bool OnLoad(std::string_view, MaterialSetEntry&) { return false; }
    void OnFree(MaterialSetEntry& entry);
    bool IsEntryLive(const MaterialSetEntry& entry) const { return entry.Alive; }

    MaterialCache* Materials = nullptr;
};
