#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// What a client guessed ahead of the authority, each guess on the command tick it
// was made for, kept until the authority has run that tick and so decided it.
// Bounded: past its capacity nothing more is guessed, so every guess in the
// world is one this journal will decide. Owner-thread only.
template <typename T>
class PredictionJournal
{
public:
    explicit PredictionJournal(std::size_t capacity)
        : Capacity(capacity)
    {
        Entries.reserve(capacity);
    }

    [[nodiscard]] bool Full() const { return Entries.size() >= Capacity; }
    [[nodiscard]] std::size_t Size() const { return Entries.size(); }
    void Clear() { Entries.clear(); }

    // False, guessing nothing, when the journal is full.
    bool Add(T guess, std::uint64_t command)
    {
        if (Full())
            return false;
        Entries.push_back(Entry{ std::move(guess), command });
        return true;
    }

    // The first guess `match` accepts, to update in place; null for none.
    template <typename Match>
    [[nodiscard]] T* Find(Match&& match)
    {
        for (Entry& entry : Entries)
            if (match(entry.Value))
                return &entry.Value;
        return nullptr;
    }

    // Call after applying a snapshot with the last command the authority ran: each
    // guess it has decided goes to `decided` and is dropped, unless `decided` says to
    // keep it; each it has not yet goes to `undecided`. Callbacks take T&.
    template <typename Decided, typename Undecided>
    void Reconcile(std::uint64_t acknowledged, Decided&& decided, Undecided&& undecided)
    {
        std::erase_if(Entries, [&](Entry& entry) {
            if (entry.Command > acknowledged)
                return false;
            return !decided(entry.Value);
        });
        for (Entry& entry : Entries)
            if (entry.Command > acknowledged)
                undecided(entry.Value);
    }

private:
    struct Entry
    {
        T Value;
        std::uint64_t Command = 0;
    };

    std::vector<Entry> Entries;
    std::size_t Capacity = 0;
};
