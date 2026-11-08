#pragma once

#include <render/RenderEntityKey.h>

#include <cstdint>
#include <span>
#include <vector>

// One visible skinned instance competing for this frame's pose dispatches.
struct SkinnedPoseCandidate
{
    RenderEntityKey Key;
    float CameraDepth = 0.0f;
    std::uint32_t JointCount = 0;
    // Frames since its last pose dispatch; ignored for one that has never had a pose.
    std::uint32_t FramesWaited = 0;
    bool NeverPosed = false;
};

struct SkinnedPoseBudget
{
    std::uint32_t Dispatches = 0;
    std::uint64_t PaletteBytes = 0;
};

// Bytes one instance's palette occupies in the palette upload, alignment included.
[[nodiscard]] std::uint64_t SkinnedPalettePackedBytes(std::uint32_t jointCount);

// Picks which candidates are posed this frame and writes their indices to
// `chosen`. Never-posed instances come first, one budget share goes to whoever
// has waited longest, and the rest go nearest first. Deterministic.
void SelectSkinnedPoses(std::span<const SkinnedPoseCandidate> candidates,
                        const SkinnedPoseBudget& budget,
                        std::vector<std::uint32_t>& chosen);
