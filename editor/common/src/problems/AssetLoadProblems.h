#pragma once

#include <cstdint>
#include <limits>

class AssetSystem;
class ProblemList;

// Every asset whose load failed, as a problem that says why and names the
// asset, republished when that set changes.
class AssetLoadProblems
{
public:
    explicit AssetLoadProblems(ProblemList& problems) : Problems(problems) {}

    void Update(const AssetSystem& assets);

private:
    ProblemList& Problems;
    std::uint64_t Published = std::numeric_limits<std::uint64_t>::max();
};
