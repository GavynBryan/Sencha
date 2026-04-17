#pragma once

#include <cstdint>
#include <vector>

using Id = uint32_t;
constexpr Id InvalidId = UINT32_MAX;

template <typename T>
struct SparseSet
{
   void Insert(const T& item, Id id)
   {
        Dense.push_back(item);
        Ids.push_back(id);

        if(id >= Sparse.size()) {
            Sparse.resize(id + 1, InvalidId);
        }
        
        // If the id is already present, update the item in place. 
        if(Sparse[id] != InvalidId) {
            Dense[id] = item;
            return;
        }

        Sparse[id] = Dense.size();
        Dense.push_back(item);
        ParallelOwners.push_back(id);
   }

   T* TryGet(Id id)
   {
        if(id >= Sparse.size() || Sparse[id] == InvalidId) {
            return nullptr;
        }

        return &Dense[Sparse[id]];
   }

    std::vector<T>& GetItems() { return Dense; }
    const std::vector<T>& GetItems() const { return Dense; }

    std::vector<Id>& GetOwners() { return ParallelOwners; }
    const std::vector<Id>& GetOwners() const { return ParallelOwners; }

private:    
    std::vector<T> Dense;
    std::vector<Id> Sparse; 
    std::vector<Id> ParallelOwners;
};
