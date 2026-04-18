#pragma once

#include <world/entity/EntityId.h>
#include <world/transform/TransformHierarchyService.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

struct DenseBitset
{
    void Resize(size_t count)
    {
        const size_t words = (count + 63) / 64;
        Words.assign(words, ~uint64_t{0});
    }

    void ClearAll()
    {
        for (auto& word : Words)
            word = 0;
    }

    void Set(size_t index)
    {
        Words[index >> 6] |= uint64_t{1} << (index & 63);
    }

    bool Test(size_t index) const
    {
        return (Words[index >> 6] >> (index & 63)) & 1;
    }

    bool IsAllClear() const
    {
        for (auto word : Words)
        {
            if (word)
                return false;
        }
        return true;
    }

    std::vector<uint64_t> Words;
};

//=============================================================================
// TransformPropagationOrderService
//
// Caches a parent-before-child traversal over the transform hierarchy. Entries
// store dense indices into TransformStore's sparse-set component array, so the
// propagation loop remains a straight sweep with no hash lookups.
//=============================================================================
class TransformPropagationOrderService
{
public:
    static constexpr uint32_t NullIndex = UINT32_MAX;

    struct PropagationEntry
    {
        uint32_t TransformIndex;
        uint32_t ParentTransformIndex;
    };

    template <typename TTransformStore>
    void MaybeRebuild(const TransformHierarchyService& hierarchy, const TTransformStore& transforms)
    {
        const uint64_t hierarchyVersion = hierarchy.GetVersion();
        const uint64_t transformVersion = transforms.GetVersion();

        if (hierarchyVersion == CachedHierarchyVersion
            && transformVersion == CachedTransformVersion)
        {
            return;
        }

        Rebuild(hierarchy, transforms);

        CachedHierarchyVersion = hierarchyVersion;
        CachedTransformVersion = transformVersion;
    }

    std::span<const PropagationEntry> GetOrder() const
    {
        return std::span<const PropagationEntry>(Order);
    }

    void MarkLocalDirty(uint32_t transformIndex)
    {
        if (transformIndex < LocalDirty.Words.size() * 64)
            LocalDirty.Set(transformIndex);
    }

    bool IsAllClean() const { return LocalDirty.IsAllClear(); }

    const DenseBitset& GetLocalDirty()   const { return LocalDirty;   }
          DenseBitset& GetLocalDirty()         { return LocalDirty;   }
    const DenseBitset& GetWorldChanged() const { return WorldChanged; }
          DenseBitset& GetWorldChanged()       { return WorldChanged; }

private:
    struct QueueEntry
    {
        EntityId Entity;
        uint32_t ParentTransformIndex;
    };

    template <typename TTransformStore>
    void Rebuild(const TransformHierarchyService& hierarchy, const TTransformStore& transforms)
    {
        Order.clear();
        Order.reserve(transforms.Count());

        VisitQueue.clear();
        for (EntityId root : hierarchy.GetRoots())
            VisitQueue.push_back({ root, NullIndex });

        for (size_t head = 0; head < VisitQueue.size(); ++head)
        {
            const QueueEntry current = VisitQueue[head];
            const uint32_t transformIndex = transforms.IndexOf(current.Entity);
            if (transformIndex == NullIndex)
                continue;

            Order.push_back({ transformIndex, current.ParentTransformIndex });

            const auto& children = hierarchy.GetChildren(current.Entity);
            for (EntityId child : children)
                VisitQueue.push_back({ child, transformIndex });
        }

        LocalDirty.Resize(transforms.Count());
        WorldChanged.Resize(transforms.Count());
    }

    std::vector<PropagationEntry> Order;
    std::vector<QueueEntry> VisitQueue;
    DenseBitset LocalDirty;
    DenseBitset WorldChanged;

    uint64_t CachedHierarchyVersion = UINT64_MAX;
    uint64_t CachedTransformVersion = UINT64_MAX;
};
