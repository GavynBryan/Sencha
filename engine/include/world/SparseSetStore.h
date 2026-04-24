#pragma once

#include <ecs/EntityId.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

//=============================================================================
// SparseSetStore<T>
//
// Migration-only compatibility store for older tests and examples. Production
// ECS code should use World archetype components directly.
//=============================================================================
template <typename T>
class SparseSetStore
{
public:
    bool Add(EntityId entity, const T& value = T{})
    {
        if (OwnerToIndex.contains(entity.Index))
            return false;

        OwnerToIndex[entity.Index] = static_cast<uint32_t>(Items.size());
        Owners.push_back(entity.Index);
        Items.push_back(value);
        ++Version;
        return true;
    }

    bool Remove(EntityId entity)
    {
        auto it = OwnerToIndex.find(entity.Index);
        if (it == OwnerToIndex.end())
            return false;

        const uint32_t index = it->second;
        const uint32_t last = static_cast<uint32_t>(Items.size() - 1);
        if (index != last)
        {
            Items[index] = Items[last];
            Owners[index] = Owners[last];
            OwnerToIndex[Owners[index]] = index;
        }

        Items.pop_back();
        Owners.pop_back();
        OwnerToIndex.erase(it);
        ++Version;
        return true;
    }

    T* TryGet(EntityId entity)
    {
        auto it = OwnerToIndex.find(entity.Index);
        return it == OwnerToIndex.end() ? nullptr : &Items[it->second];
    }

    const T* TryGet(EntityId entity) const
    {
        auto it = OwnerToIndex.find(entity.Index);
        return it == OwnerToIndex.end() ? nullptr : &Items[it->second];
    }

    uint32_t IndexOf(EntityId entity) const
    {
        auto it = OwnerToIndex.find(entity.Index);
        return it == OwnerToIndex.end() ? UINT32_MAX : it->second;
    }

    size_t Count() const { return Items.size(); }
    uint64_t GetVersion() const { return Version; }
    std::span<T> GetItems() { return Items; }
    std::span<const T> GetItems() const { return Items; }
    std::span<const EntityIndex> GetOwnerIds() const { return Owners; }

private:
    std::vector<T> Items;
    std::vector<EntityIndex> Owners;
    std::unordered_map<EntityIndex, uint32_t> OwnerToIndex;
    uint64_t Version = 0;
};
