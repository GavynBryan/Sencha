#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <presentation/PresentationOwner.h>
#include <render/Material.h>
#include <render/MaterialSetHandle.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <vector>

class World;
struct GpuStaticMesh;

// Writers hide and substitute an entity's material slots through their own
// contributions; taking one back restores what was authored, which is never
// written. See docs/renderer/mesh-overrides.md.

// Which material slots a hide or a substitution names: those drawing an
// authored material, which survives re-export and renames, or one slot.
struct MeshSlotSelector
{
    enum class By : std::uint8_t
    {
        DrawnMaterial,
        Slot,
    };
    By Kind = By::DrawnMaterial;
    MaterialHandle Material;
    std::uint8_t Slot = 0;
};

struct MeshMaterialSubstitution
{
    MeshSlotSelector Slots;
    MaterialHandle Material;
};

struct MeshOverrideContribution
{
    PresentationOwner Owner;
    // A slot two contributions substitute goes to the higher Order.
    std::int16_t Order = 0;
    std::vector<MeshSlotSelector> Hide;
    std::vector<MeshMaterialSubstitution> Substitute;
};

enum class MeshOverrideResult : std::uint8_t
{
    Applied,
    NoMesh,
};

struct MeshOverrideOutcome
{
    MeshOverrideResult Result = MeshOverrideResult::Applied;
    // Two contributions at one Order substitute different materials into a slot;
    // the earlier owner by PresentationOwnerPrecedes wins.
    bool Tied = false;
    PresentationOwner TieWinner;
    // A Slot selector past the authored set, which names nothing.
    bool SlotOutOfRange = false;
};

// What extraction draws in place of the authored state. Added at an entity's
// first contribution and kept, so contributing again never moves it between
// archetypes. Runtime only: never saved, replicated or authored.
struct SENCHA_COMPONENT("sencha.mesh_overrides") MeshOverrides
{
    // The authored slot materials with every substitution applied, owned here;
    // empty when nothing substitutes.
    MaterialSetHandle Materials;
    // The authored set this was composed from. While the authored set differs,
    // extraction draws it unchanged until the overrides are composed again.
    MaterialSetHandle ComposedFrom;
    std::uint32_t HiddenSlots = 0;
};

static_assert(std::is_trivially_copyable_v<MeshOverrides>,
              "MeshOverrides must be trivially copyable to live in ECS chunks");

template <>
struct ComponentTraits<MeshOverrides>
{
    static void OnRemove(const MeshOverrides& overrides, World& world, EntityId entity);
};

// Every entity's contributions, beside the World rather than in its rows. A
// World resource; MeshOverrides' removal forgets an entity's.
struct MeshOverrideContributions
{
    std::unordered_map<EntityId, std::vector<MeshOverrideContribution>, EntityIdHash> ByEntity;
};

// Adds or replaces `contribution.Owner`'s contribution and composes. Structural
// on an entity's first contribution, so outside any query.
MeshOverrideOutcome SetMeshOverride(World& world, EntityId entity, MeshOverrideContribution contribution);
void ClearMeshOverride(World& world, EntityId entity, const PresentationOwner& owner);
// Recomposes against the entity's current authored set.
MeshOverrideOutcome ComposeMeshOverrides(World& world, EntityId entity);

struct EffectiveMeshDraw
{
    MaterialSetHandle Materials;
    std::uint32_t SectionMask = 0;
};

// The slot materials and sections an entity draws and casts shadows with. The
// one place extraction applies overrides, so a mesh and its shadow agree.
[[nodiscard]] EffectiveMeshDraw ApplyMeshOverrides(const MeshOverrides* overrides,
                                                   MaterialSetHandle authoredMaterials,
                                                   std::uint32_t authoredSectionMask,
                                                   const GpuStaticMesh& mesh,
                                                   std::size_t slotCount);

#if !defined(SENCHA_CODEGEN)
#  include <render/MeshOverrides.sencha.h>
#endif
