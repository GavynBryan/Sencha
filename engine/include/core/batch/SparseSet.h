#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

using Id = uint32_t;
constexpr Id InvalidId = UINT32_MAX;

template <typename T>
struct SparseSet
{
    static_assert(std::is_move_assignable_v<T>,
        "SparseSet<T> requires move assignment for swap-remove and upsert.");

    static constexpr Id InvalidIndex = InvalidId;

    void Insert(const T& item, Id id)
    {
        Upsert(id, item);
    }

    void Insert(T&& item, Id id)
    {
        Upsert(id, std::move(item));
    }

    template <typename... Args>
    T& Emplace(Id id, Args&&... args)
    {
        assert(id != InvalidId && "SparseSet cannot store InvalidId.");
        EnsureSparseCapacity(id);

        const Id denseIndex = Sparse[id];
        if (denseIndex != InvalidIndex)
        {
            assert(IsLiveDenseIndex(id, denseIndex));
            Dense[denseIndex] = T(std::forward<Args>(args)...);
            ++VersionCounter;
            return Dense[denseIndex];
        }

        const Id newDenseIndex = static_cast<Id>(Dense.size());
        assert(static_cast<size_t>(newDenseIndex) == Dense.size()
            && "SparseSet dense index overflow.");

        Sparse[id] = newDenseIndex;
        Dense.emplace_back(std::forward<Args>(args)...);
        ParallelOwners.push_back(id);
        ++VersionCounter;
        return Dense.back();
    }

    bool Remove(Id id)
    {
        const Id denseIndex = IndexOf(id);
        if (denseIndex == InvalidIndex)
            return false;

        const Id lastIndex = static_cast<Id>(Dense.size() - 1);
        if (denseIndex != lastIndex)
        {
            const Id movedOwner = ParallelOwners[lastIndex];
            Dense[denseIndex] = std::move(Dense[lastIndex]);
            ParallelOwners[denseIndex] = movedOwner;
            Sparse[movedOwner] = denseIndex;
        }

        Dense.pop_back();
        ParallelOwners.pop_back();
        Sparse[id] = InvalidIndex;
        ++VersionCounter;
        return true;
    }

    void Clear()
    {
        if (Dense.empty())
            return;

        for (Id owner : ParallelOwners)
            Sparse[owner] = InvalidIndex;

        Dense.clear();
        ParallelOwners.clear();
        ++VersionCounter;
    }

    T* TryGet(Id id)
    {
        const Id denseIndex = IndexOf(id);
        return denseIndex == InvalidIndex ? nullptr : &Dense[denseIndex];
    }

    const T* TryGet(Id id) const
    {
        const Id denseIndex = IndexOf(id);
        return denseIndex == InvalidIndex ? nullptr : &Dense[denseIndex];
    }

    bool Contains(Id id) const
    {
        return IndexOf(id) != InvalidIndex;
    }

    Id IndexOf(Id id) const
    {
        if (id >= Sparse.size())
            return InvalidIndex;

        const Id denseIndex = Sparse[id];
        if (denseIndex == InvalidIndex || !IsLiveDenseIndex(id, denseIndex))
            return InvalidIndex;

        return denseIndex;
    }

    void Reserve(size_t capacity)
    {
        Dense.reserve(capacity);
        ParallelOwners.reserve(capacity);
    }

    void ReserveSparse(Id maxId)
    {
        EnsureSparseCapacity(maxId);
    }

    size_t Count() const { return Dense.size(); }
    bool IsEmpty() const { return Dense.empty(); }
    uint64_t GetVersion() const { return VersionCounter; }

    std::vector<T>& GetItems() { return Dense; }
    const std::vector<T>& GetItems() const { return Dense; }

    std::vector<Id>& GetOwners() { return ParallelOwners; }
    const std::vector<Id>& GetOwners() const { return ParallelOwners; }

private:    
    template <typename TValue>
    void Upsert(Id id, TValue&& item)
    {
        Emplace(id, std::forward<TValue>(item));
    }

    void EnsureSparseCapacity(Id id)
    {
        assert(id != InvalidId && "SparseSet cannot store InvalidId.");
        if (id >= Sparse.size())
            Sparse.resize(static_cast<size_t>(id) + 1, InvalidIndex);
    }

    bool IsLiveDenseIndex(Id id, Id denseIndex) const
    {
        return denseIndex < Dense.size()
            && denseIndex < ParallelOwners.size()
            && ParallelOwners[denseIndex] == id;
    }

    std::vector<T> Dense;
    std::vector<Id> Sparse; 
    std::vector<Id> ParallelOwners;
    uint64_t VersionCounter = 0;
};
