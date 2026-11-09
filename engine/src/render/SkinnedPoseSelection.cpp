#include <render/SkinnedPoseSelection.h>

#include <render/SkinnedPoseFrameData.h>

#include <algorithm>
#include <cstddef>

namespace
{
constexpr std::uint32_t kAgedShareDivisor = 8;

struct Selection
{
    std::span<const SkinnedPoseCandidate> Candidates;
    SkinnedPoseBudget Budget;
    std::vector<std::uint32_t>& Chosen;
    std::vector<std::uint8_t> Taken;
    std::uint64_t PaletteBytes = 0;

    [[nodiscard]] bool Full() const { return Chosen.size() >= Budget.Dispatches; }

    void TakeFrom(std::span<const std::uint32_t> order, std::uint32_t limit)
    {
        std::uint32_t taken = 0;
        for (const std::uint32_t index : order)
        {
            if (Full() || taken >= limit)
                return;
            if (Taken[index] != 0)
                continue;
            const std::uint64_t bytes = SkinnedPalettePackedBytes(Candidates[index].JointCount);
            if (PaletteBytes + bytes > Budget.PaletteBytes)
                continue;
            Taken[index] = 1;
            PaletteBytes += bytes;
            Chosen.push_back(index);
            ++taken;
        }
    }
};
}

std::uint64_t SkinnedPalettePackedBytes(std::uint32_t jointCount)
{
    const std::uint64_t joints = (static_cast<std::uint64_t>(jointCount) + kPaletteJointAlignment - 1)
        / kPaletteJointAlignment * kPaletteJointAlignment;
    return joints * kPaletteMatrixFloats * sizeof(float);
}

void SelectSkinnedPoses(std::span<const SkinnedPoseCandidate> candidates,
                        const SkinnedPoseBudget& budget,
                        std::vector<std::uint32_t>& chosen)
{
    chosen.clear();
    if (candidates.empty() || budget.Dispatches == 0)
        return;

    std::vector<std::uint32_t> nearest(candidates.size());
    for (std::uint32_t index = 0; index < nearest.size(); ++index)
        nearest[index] = index;
    std::ranges::sort(nearest, [&](std::uint32_t a, std::uint32_t b) {
        if (candidates[a].CameraDepth != candidates[b].CameraDepth)
            return candidates[a].CameraDepth < candidates[b].CameraDepth;
        return candidates[a].Key < candidates[b].Key;
    });

    std::vector<std::uint32_t> neverPosed;
    std::vector<std::uint32_t> longestWaiting = nearest;
    for (const std::uint32_t index : nearest)
        if (candidates[index].NeverPosed)
            neverPosed.push_back(index);
    std::ranges::stable_sort(longestWaiting, [&](std::uint32_t a, std::uint32_t b) {
        return candidates[a].FramesWaited > candidates[b].FramesWaited;
    });

    Selection selection{ candidates, budget, chosen, std::vector<std::uint8_t>(candidates.size(), 0) };
    selection.TakeFrom(neverPosed, budget.Dispatches);
    selection.TakeFrom(longestWaiting, std::max(1u, budget.Dispatches / kAgedShareDivisor));
    selection.TakeFrom(nearest, budget.Dispatches);
}
