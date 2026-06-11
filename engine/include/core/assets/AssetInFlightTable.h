#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

//=============================================================================
// AssetInFlightTable<TWaiter>
//
// Owner-thread bookkeeping for staged loads that have been submitted but not
// yet committed (docs/assets/pipeline.md, Decision C: dedup happens before
// work is submitted). Two requesters of the same path coalesce on one load:
// the first Begin() returns Started and submits the work; later Begin()s
// return Joined and submit nothing. Every Begin records its waiter; Finish()
// removes the entry and hands back all waiters, so the committer can give
// each one its ref-counted handle (or its failure notice).
//
// Owner-thread state only — no locks, by the usual argument.
//=============================================================================
template <typename TWaiter>
class AssetInFlightTable
{
public:
    enum class BeginResult : uint8_t
    {
        Started, // first requester: caller submits the load
        Joined,  // load already in flight: caller submits nothing
    };

    BeginResult Begin(std::string_view path, TWaiter waiter)
    {
        auto [it, inserted] = Waiters.try_emplace(std::string(path));
        it->second.push_back(std::move(waiter));
        return inserted ? BeginResult::Started : BeginResult::Joined;
    }

    [[nodiscard]] bool IsInFlight(std::string_view path) const
    {
        return Waiters.find(std::string(path)) != Waiters.end();
    }

    // Removes the entry, returning every waiter that joined (empty if the
    // path was not in flight). Called on commit and on abort alike.
    [[nodiscard]] std::vector<TWaiter> Finish(std::string_view path)
    {
        auto it = Waiters.find(std::string(path));
        if (it == Waiters.end())
            return {};

        std::vector<TWaiter> result = std::move(it->second);
        Waiters.erase(it);
        return result;
    }

    [[nodiscard]] std::size_t Size() const { return Waiters.size(); }

private:
    std::unordered_map<std::string, std::vector<TWaiter>> Waiters;
};
