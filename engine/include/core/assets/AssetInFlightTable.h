#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

//=============================================================================
// AssetInFlightTable
//
// Owner-thread bookkeeping for staged loads that have been submitted but not
// yet committed (docs/assets/pipeline.md, Decision C: dedup happens before
// work is submitted). Two requesters of the same path coalesce on one load:
// the first Begin() returns Started and submits the work; later Begin()s
// return Joined and submit nothing. Finish() removes the entry and returns
// how many requesters joined, so the committer can apply that many retains
// (or report that many failures).
//
// Owner-thread state only — no locks, by the usual argument. The async
// consumer arrives in Stage 3; until then the synchronous path never has an
// in-flight window and does not use this table.
//=============================================================================
class AssetInFlightTable
{
public:
    enum class BeginResult : uint8_t
    {
        Started, // first requester: caller submits the load
        Joined,  // load already in flight: caller submits nothing
    };

    BeginResult Begin(std::string_view path)
    {
        auto [it, inserted] = Requesters.try_emplace(std::string(path), 0u);
        ++it->second;
        return inserted ? BeginResult::Started : BeginResult::Joined;
    }

    [[nodiscard]] bool IsInFlight(std::string_view path) const
    {
        return Requesters.find(std::string(path)) != Requesters.end();
    }

    // Removes the entry, returning the total requester count (0 if the path
    // was not in flight). Called on commit and on abort alike — the count is
    // how many retains to apply or how many failures to report.
    uint32_t Finish(std::string_view path)
    {
        auto it = Requesters.find(std::string(path));
        if (it == Requesters.end())
            return 0;

        const uint32_t count = it->second;
        Requesters.erase(it);
        return count;
    }

    [[nodiscard]] std::size_t Size() const { return Requesters.size(); }

private:
    std::unordered_map<std::string, uint32_t> Requesters;
};
