#include <render/MeshOverrides.h>

#include <core/assets/AssetStoreTable.h>
#include <ecs/World.h>
#include <render/SectionMaterial.h>
#include <render/StaticMeshComponent.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <render/static_mesh/GpuStaticMesh.h>

#include <algorithm>
#include <cassert>
#include <optional>

namespace
{
    IAssetStore* MaterialStore(const World& world)
    {
        const AssetStoreTable* stores = world.TryGetResource<AssetStoreTable>();
        return stores != nullptr ? stores->Find(AssetType::Material, AssetArity::Single) : nullptr;
    }

    IAssetListStore* MaterialListStore(const World& world)
    {
        const AssetStoreTable* stores = world.TryGetResource<AssetStoreTable>();
        return stores != nullptr ? stores->FindList(AssetType::Material) : nullptr;
    }

    // A contribution keeps every material it substitutes resident, so one that
    // loses a slot now still draws when the winner is taken back.
    void RetainSubstitutes(const World& world, const MeshOverrideContribution& contribution)
    {
        if (IAssetStore* store = MaterialStore(world))
            for (const MeshMaterialSubstitution& substitution : contribution.Substitute)
                store->RetainToken(substitution.Material.ToToken());
    }

    void ReleaseSubstitutes(const World& world, const MeshOverrideContribution& contribution)
    {
        if (IAssetStore* store = MaterialStore(world))
            for (const MeshMaterialSubstitution& substitution : contribution.Substitute)
                store->ReleaseToken(substitution.Material.ToToken());
    }

    void ReleaseComposedSet(const World& world, MaterialSetHandle set)
    {
        if (IAssetListStore* store = MaterialListStore(world); store != nullptr && set.IsValid())
            store->ReleaseToken(set.ToToken());
    }

    // Skinned first: an entity drawing both composes against the one it is posed with.
    std::optional<MaterialSetHandle> AuthoredSet(const World& world, EntityId entity)
    {
        if (world.IsRegistered<SkinnedMeshComponent>())
            if (const SkinnedMeshComponent* skinned = world.TryGet<SkinnedMeshComponent>(entity))
                return skinned->Materials;
        if (world.IsRegistered<StaticMeshComponent>())
            if (const StaticMeshComponent* mesh = world.TryGet<StaticMeshComponent>(entity))
                return mesh->Materials;
        return std::nullopt;
    }

    // Bit j set for every slot of `slots` the selector names.
    std::uint32_t SelectSlots(const MeshSlotSelector& selector, const std::vector<MaterialHandle>& slots,
                              bool& outOfRange)
    {
        const std::size_t count = std::min<std::size_t>(slots.size(), 32);
        if (selector.Kind == MeshSlotSelector::By::Slot)
        {
            if (selector.Slot >= count)
            {
                outOfRange = true;
                return 0;
            }
            return 1u << selector.Slot;
        }
        std::uint32_t mask = 0;
        for (std::size_t j = 0; j < count; ++j)
            if (slots[j] == selector.Material)
                mask |= 1u << j;
        return mask;
    }

    struct SlotWinner
    {
        const MeshOverrideContribution* Contribution = nullptr;
        MaterialHandle Material;
    };
}

void ComponentTraits<MeshOverrides>::OnRemove(const MeshOverrides& overrides, World& world, EntityId entity)
{
    ReleaseComposedSet(world, overrides.Materials);
    MeshOverrideContributions* contributions = world.TryGetResource<MeshOverrideContributions>();
    if (contributions == nullptr)
        return;
    if (const auto found = contributions->ByEntity.find(entity); found != contributions->ByEntity.end())
    {
        for (const MeshOverrideContribution& contribution : found->second)
            ReleaseSubstitutes(world, contribution);
        contributions->ByEntity.erase(found);
    }
}

MeshOverrideOutcome ComposeMeshOverrides(World& world, EntityId entity)
{
    MeshOverrideOutcome outcome;
    MeshOverrides* overrides = world.TryGet<MeshOverrides>(entity);
    const std::optional<MaterialSetHandle> authored = AuthoredSet(world, entity);
    if (overrides == nullptr || !authored.has_value())
    {
        outcome.Result = MeshOverrideResult::NoMesh;
        return outcome;
    }

    std::vector<MaterialHandle> slots;
    IAssetListStore* listStore = MaterialListStore(world);
    if (listStore != nullptr)
        for (const std::uint64_t token : listStore->ListMembers(authored->ToToken()))
            slots.push_back(MaterialHandle::FromToken(token));

    const std::vector<MeshOverrideContribution>* held = nullptr;
    if (const MeshOverrideContributions* all = world.TryGetResource<MeshOverrideContributions>())
        if (const auto found = all->ByEntity.find(entity); found != all->ByEntity.end())
            held = &found->second;

    std::uint32_t hidden = 0;
    std::vector<SlotWinner> winners(slots.size());
    if (held != nullptr)
        for (const MeshOverrideContribution& contribution : *held)
        {
            for (const MeshSlotSelector& selector : contribution.Hide)
                hidden |= SelectSlots(selector, slots, outcome.SlotOutOfRange);
            for (const MeshMaterialSubstitution& substitution : contribution.Substitute)
            {
                const std::uint32_t named = SelectSlots(substitution.Slots, slots, outcome.SlotOutOfRange);
                for (std::size_t j = 0; j < winners.size() && j < 32; ++j)
                {
                    if ((named & (1u << j)) == 0)
                        continue;
                    SlotWinner& winner = winners[j];
                    if (winner.Contribution == nullptr || contribution.Order > winner.Contribution->Order)
                    {
                        winner = { &contribution, substitution.Material };
                        continue;
                    }
                    if (contribution.Order < winner.Contribution->Order || substitution.Material == winner.Material)
                        continue;
                    outcome.Tied = true;
                    if (PresentationOwnerPrecedes(contribution.Owner, winner.Contribution->Owner))
                        winner = { &contribution, substitution.Material };
                    outcome.TieWinner = winner.Contribution->Owner;
                }
            }
        }

    MaterialSetHandle composed;
    if (std::any_of(winners.begin(), winners.end(), [](const SlotWinner& w) { return w.Contribution != nullptr; })
        && listStore != nullptr)
    {
        std::vector<std::uint64_t> tokens;
        tokens.reserve(slots.size());
        for (std::size_t j = 0; j < slots.size(); ++j)
            tokens.push_back((winners[j].Contribution != nullptr ? winners[j].Material : slots[j]).ToToken());
        composed = MaterialSetHandle::FromToken(listStore->InternList(tokens).Relinquish());
    }

    ReleaseComposedSet(world, overrides->Materials);
    overrides->Materials = composed;
    overrides->ComposedFrom = *authored;
    overrides->HiddenSlots = hidden;
    return outcome;
}

MeshOverrideOutcome SetMeshOverride(World& world, EntityId entity, MeshOverrideContribution contribution)
{
    if (!AuthoredSet(world, entity).has_value())
    {
        MeshOverrideOutcome refused;
        refused.Result = MeshOverrideResult::NoMesh;
        return refused;
    }
    if (!world.HasResource<MeshOverrideContributions>())
        world.AddResource<MeshOverrideContributions>();
    if (!world.HasComponent<MeshOverrides>(entity))
        world.AddComponent<MeshOverrides>(entity);

    RetainSubstitutes(world, contribution);
    std::vector<MeshOverrideContribution>& held = world.GetResource<MeshOverrideContributions>().ByEntity[entity];
    const auto existing = std::find_if(held.begin(), held.end(), [&](const MeshOverrideContribution& c) {
        return c.Owner == contribution.Owner;
    });
    if (existing != held.end())
    {
        ReleaseSubstitutes(world, *existing);
        *existing = std::move(contribution);
    }
    else
        held.push_back(std::move(contribution));
    return ComposeMeshOverrides(world, entity);
}

void ClearMeshOverride(World& world, EntityId entity, const PresentationOwner& owner)
{
    MeshOverrideContributions* contributions = world.TryGetResource<MeshOverrideContributions>();
    if (contributions == nullptr)
        return;
    const auto found = contributions->ByEntity.find(entity);
    if (found == contributions->ByEntity.end())
        return;
    std::vector<MeshOverrideContribution>& held = found->second;
    const auto existing = std::find_if(held.begin(), held.end(),
                                       [&](const MeshOverrideContribution& c) { return c.Owner == owner; });
    if (existing == held.end())
        return;
    ReleaseSubstitutes(world, *existing);
    held.erase(existing);
    if (held.empty())
        contributions->ByEntity.erase(found);
    (void)ComposeMeshOverrides(world, entity);
}

EffectiveMeshDraw ApplyMeshOverrides(const MeshOverrides* overrides, MaterialSetHandle authoredMaterials,
                                     std::uint32_t authoredSectionMask, const GpuStaticMesh& mesh,
                                     std::size_t slotCount)
{
    if (overrides == nullptr || overrides->ComposedFrom != authoredMaterials)
        return { authoredMaterials, authoredSectionMask };

    std::uint32_t hiddenSections = 0;
    if (overrides->HiddenSlots != 0 && slotCount != 0)
        for (std::uint32_t section = 0; section < mesh.Sections.size() && section < 32; ++section)
        {
            const std::uint32_t slot = ResolveSectionSlot(mesh, section, slotCount);
            if (slot < 32 && (overrides->HiddenSlots & (1u << slot)) != 0)
                hiddenSections |= 1u << section;
        }
    return { overrides->Materials.IsValid() ? overrides->Materials : authoredMaterials,
             authoredSectionMask & ~hiddenSections };
}
