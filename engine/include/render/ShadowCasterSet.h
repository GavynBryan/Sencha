#pragma once

#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>
#include <render/Material.h>
#include <render/MaterialSetCache.h>
#include <render/RenderEntityKey.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <cstdint>
#include <vector>

struct ShadowCasterItem
{
    StaticMeshHandle Mesh;
    MaterialHandle Material;
    std::uint32_t SectionIndex = 0;
    Mat4 WorldMatrix = Mat4::Identity();
    Aabb3d WorldBounds = Aabb3d::Empty();
    bool DoubleSided = false;
};

// Everything extracted for one caster entity that can change its shadow
// silhouette or shadow-pass pipeline. Bounds are quantized so float noise in
// transform propagation cannot masquerade as movement; the material hash
// covers only shadow-relevant state (per-section cast and double-sided), so
// a base-color edit does not read as a change.
struct ShadowCasterState
{
    Aabb3d WorldBounds = Aabb3d::Empty();
    StaticMeshHandle Mesh;
    MaterialSetHandle Materials;
    std::uint32_t EffectiveShadowSectionMask = 0;
    std::uint64_t ShadowMaterialStateHash = 0;

    bool operator==(const ShadowCasterState&) const = default;
};

struct ShadowCasterRecord
{
    RenderEntityKey Key;
    ShadowCasterState State;
};

// One silhouette change between consecutive frames. Consumers only need the
// affected bounds; Change exists for tests and counters.
struct ShadowCasterEvent
{
    enum class Kind : std::uint8_t
    {
        Added,
        Removed,
        Changed,
    };

    Kind Change = Kind::Added;
    // Removed events carry the previous bounds and Changed events the union
    // of previous and current, so a caster leaving a shadow volume still
    // invalidates it at the departure site.
    Aabb3d Bounds = Aabb3d::Empty();
};

[[nodiscard]] Aabb3d QuantizeShadowCasterBounds(const Aabb3d& bounds);

struct ShadowCasterSet
{
    std::vector<ShadowCasterItem> Items;
    // One record per caster entity that contributed at least one item.
    std::vector<ShadowCasterRecord> Records;

    void Reset()
    {
        Items.clear();
        Records.clear();
    }
};

//=============================================================================
// ShadowCasterDiff
//
// Sorted-merge diff of consecutive per-entity caster tables. Apply consumes
// the frame's records (sorting them by key), emits added/removed/changed
// events against the retained previous table, then swaps the tables. The
// previous table is replaced only here, so a zone detach still emits removed
// events from its final extracted state. Skipping event emission still swaps,
// keeping the table current while no consumer wants events.
//=============================================================================
class ShadowCasterDiff
{
public:
    void Apply(std::vector<ShadowCasterRecord>& current,
               bool emitEvents,
               std::vector<ShadowCasterEvent>& events);

private:
    std::vector<ShadowCasterRecord> Previous;
};
