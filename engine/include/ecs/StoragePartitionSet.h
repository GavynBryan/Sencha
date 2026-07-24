#pragma once

#include <ecs/StoragePartitionId.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Dense membership set for storage-partition filtering.
//
// Contains() is one indexed word test per chunk. Members() preserves insertion
// order for deterministic domain iteration and backend orchestration. Clear()
// retains allocated storage so a frame-owned set can be rebuilt without churn.
class StoragePartitionSet
{
public:
    bool Add(StoragePartitionId partition)
    {
        EnsureWord(partition);
        const std::size_t word = WordIndex(partition);
        const std::uint64_t bit = Bit(partition);
        if ((Words_[word] & bit) != 0)
            return false;

        Words_[word] |= bit;
        Members_.push_back(partition);
        return true;
    }

    bool Remove(StoragePartitionId partition)
    {
        const std::size_t word = WordIndex(partition);
        if (word >= Words_.size())
            return false;

        const std::uint64_t bit = Bit(partition);
        if ((Words_[word] & bit) == 0)
            return false;

        Words_[word] &= ~bit;
        const auto found = std::find(Members_.begin(), Members_.end(), partition);
        if (found != Members_.end())
            Members_.erase(found);
        return true;
    }

    [[nodiscard]] bool Contains(StoragePartitionId partition) const
    {
        const std::size_t word = WordIndex(partition);
        return word < Words_.size() && (Words_[word] & Bit(partition)) != 0;
    }

    void Clear()
    {
        std::fill(Words_.begin(), Words_.end(), std::uint64_t{ 0 });
        Members_.clear();
    }

    [[nodiscard]] bool Empty() const { return Members_.empty(); }
    [[nodiscard]] std::size_t Size() const { return Members_.size(); }

    // Membership equality, insensitive to insertion order: the word bitmap is
    // the identity and Members_ only records the order domains iterate in. Two
    // sets can also carry different word-vector lengths when one of them once
    // held a higher partition id, so the tail beyond the shorter set must be
    // empty rather than merely absent.
    [[nodiscard]] bool operator==(const StoragePartitionSet& other) const
    {
        const std::size_t shared = std::min(Words_.size(), other.Words_.size());
        for (std::size_t index = 0; index < shared; ++index)
        {
            if (Words_[index] != other.Words_[index])
                return false;
        }

        const std::vector<std::uint64_t>& longer =
            Words_.size() > other.Words_.size() ? Words_ : other.Words_;
        for (std::size_t index = shared; index < longer.size(); ++index)
        {
            if (longer[index] != 0)
                return false;
        }
        return true;
    }

    // Insertion order, which is not a stable property of a zone. RuntimeWorld
    // recycles partition slots most-recently-freed first, so which id a zone gets
    // depends on the load/unload history before it. Fine for membership tests and
    // per-chunk work; an accumulation whose result depends on visit order is not
    // reproducible across differing streaming histories. Order by ZoneId when the
    // result has to be stable.
    [[nodiscard]] std::span<const StoragePartitionId> Members() const
    {
        return { Members_.data(), Members_.size() };
    }

private:
    static constexpr std::size_t WordIndex(StoragePartitionId partition)
    {
        return static_cast<std::size_t>(partition.Value) / 64u;
    }

    static constexpr std::uint64_t Bit(StoragePartitionId partition)
    {
        return std::uint64_t{ 1 } << (partition.Value % 64u);
    }

    void EnsureWord(StoragePartitionId partition)
    {
        const std::size_t required = WordIndex(partition) + 1;
        if (Words_.size() < required)
            Words_.resize(required, std::uint64_t{ 0 });
    }

    std::vector<std::uint64_t> Words_;
    std::vector<StoragePartitionId> Members_;
};
