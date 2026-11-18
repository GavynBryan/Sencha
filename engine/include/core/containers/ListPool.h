#pragma once

#include <core/containers/SpareRetention.h>
#include <core/handle/HandlePool.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

// Lists of T that component rows name by handle, so a row costs a handle whatever
// its list holds. Every live row naming a list holds one reference to it, taken
// and given back by the component's lifecycle hooks; a list no row references is
// freed, its storage kept for the next one (docs/ecs/row-lists.md).
template <typename Tag, typename T>
class ListPool
{
public:
    using HandleType = Handle<Tag>;

    static constexpr SpareRetention kRetention{};

    struct Counters
    {
        std::size_t Live = 0;
        std::size_t Spare = 0;
        std::size_t PeakLive = 0;
        std::size_t Unreferenced = 0;
        std::uint64_t Allocated = 0;
        std::uint64_t Reused = 0;
    };

    [[nodiscard]] std::span<const T> Read(HandleType list) const
    {
        const Block* block = Blocks.Find(list);
        return block != nullptr ? std::span<const T>(block->Items) : std::span<const T>{};
    }

    // A list no row references yet: the first Retain keeps it, DropUnreferenced frees it.
    [[nodiscard]] HandleType Make(std::span<const T> items)
    {
        const HandleType list = Take();
        Blocks.Find(list)->Items.assign(items.begin(), items.end());
        Unreferenced.push_back(list);
        return list;
    }

    // The list a live row names, to write: made if the row names none, copied
    // first if another row names it too. `list` is the row's own handle.
    [[nodiscard]] std::vector<T>& Edit(HandleType& list)
    {
        Block* block = Blocks.Find(list);
        if (block != nullptr && block->References == 1)
            return block->Items;
        if (block != nullptr && block->References == 0)
        {
            Retain(list);
            return block->Items;
        }
        const HandleType own = Take();
        Block& made = *Blocks.Find(own);
        made.References = 1;
        if (block != nullptr)
        {
            made.Items = block->Items;
            --block->References;
        }
        list = own;
        return made.Items;
    }

    void Retain(HandleType list)
    {
        Block* block = Blocks.Find(list);
        if (block == nullptr)
            return;
        if (block->References++ == 0)
            std::erase(Unreferenced, list);
    }

    void Release(HandleType list)
    {
        Block* block = Blocks.Find(list);
        if (block == nullptr || block->References == 0)
            return;
        if (--block->References == 0)
            Free(list);
    }

    void DropUnreferenced()
    {
        for (const HandleType list : Unreferenced)
            Free(list);
        Unreferenced.clear();
    }

    [[nodiscard]] Counters Stats() const
    {
        Counters counts = Counts;
        counts.Live = Blocks.Size();
        counts.Spare = Spare.size();
        counts.Unreferenced = Unreferenced.size();
        return counts;
    }

private:
    struct Block
    {
        std::vector<T> Items;
        std::uint32_t References = 0;
    };

    HandleType Take()
    {
        HandleType list;
        if (!Spare.empty())
        {
            list = Blocks.Adopt(std::move(Spare.back()));
            Spare.pop_back();
            ++Counts.Reused;
        }
        else
        {
            list = Blocks.Emplace();
            ++Counts.Allocated;
        }
        Counts.PeakLive = std::max(Counts.PeakLive, Blocks.Size());
        return list;
    }

    void Free(HandleType list)
    {
        std::unique_ptr<Block> block = Blocks.Release(list);
        if (block == nullptr)
            return;
        block->Items.clear();
        block->References = 0;
        if (Spare.size() < kRetention.Limit(Blocks.Size()))
            Spare.push_back(std::move(block));
    }

    HandlePool<Tag, Block> Blocks;
    std::vector<std::unique_ptr<Block>> Spare;
    std::vector<HandleType> Unreferenced;
    Counters Counts;
};
