#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

// Which resident thumbnails to evict to reach `budget`, least-recently-used
// first. Entries used on `currentFrame` are never evicted (they are on
// screen), so the result can be smaller than the excess. Pure function of the
// per-entry last-used frames, kept free of any GPU type so the policy tests
// run headless; returns indices into `lastUsedFrames`.
[[nodiscard]] inline std::vector<std::size_t> SelectThumbnailEvictions(
    std::span<const std::uint64_t> lastUsedFrames,
    std::uint64_t currentFrame,
    std::size_t budget)
{
    if (lastUsedFrames.size() <= budget)
        return {};

    std::vector<std::size_t> order(lastUsedFrames.size());
    std::iota(order.begin(), order.end(), std::size_t{ 0 });
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b)
              {
                  if (lastUsedFrames[a] != lastUsedFrames[b])
                      return lastUsedFrames[a] < lastUsedFrames[b];
                  return a < b; // deterministic tie-break
              });

    std::vector<std::size_t> evictions;
    const std::size_t excess = lastUsedFrames.size() - budget;
    for (std::size_t index : order)
    {
        if (evictions.size() == excess)
            break;
        // Sorted ascending: once the on-screen frame is reached, everything
        // after it is on screen too.
        if (lastUsedFrames[index] == currentFrame)
            break;
        evictions.push_back(index);
    }
    return evictions;
}
