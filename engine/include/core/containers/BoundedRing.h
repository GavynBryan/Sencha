#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

// The newest Capacity() values, oldest first. Storage is sized once, so recording
// allocates nothing; a wrap counts what it overwrote, because a history that
// quietly lost its beginning explains the wrong thing.
template<typename T>
class BoundedRing
{
public:
    // At least one: a ring with no room records nothing, which reads like nothing happened.
    explicit BoundedRing(std::size_t capacity)
        : Entries(std::max<std::size_t>(capacity, 1))
    {
    }

    void Push(const T& value)
    {
        if (Live == Entries.size())
            ++Dropped;
        else
            ++Live;
        Entries[Next] = value;
        Next = (Next + 1) % Entries.size();
    }

    template<typename F>
    void ForEach(F&& visit) const
    {
        const std::size_t oldest = (Next + Entries.size() - Live) % Entries.size();
        for (std::size_t offset = 0; offset < Live; ++offset)
            visit(Entries[(oldest + offset) % Entries.size()]);
    }

    [[nodiscard]] std::vector<T> Snapshot() const
    {
        std::vector<T> ordered;
        ordered.reserve(Live);
        ForEach([&](const T& value) { ordered.push_back(value); });
        return ordered;
    }

    [[nodiscard]] std::size_t Size() const { return Live; }
    [[nodiscard]] std::size_t Capacity() const { return Entries.size(); }
    [[nodiscard]] std::size_t DroppedCount() const { return Dropped; }

    void Clear()
    {
        Next = 0;
        Live = 0;
        Dropped = 0;
    }

private:
    std::vector<T> Entries;
    std::size_t Next = 0;
    std::size_t Live = 0;
    std::size_t Dropped = 0;
};
