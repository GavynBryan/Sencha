#pragma once

#include <cstdint>

//=============================================================================
// Frame UBO range
//
// There is one set-0 binding 0 for the whole frame, and every pass that binds
// it declares how much of it its shader block covers. The descriptor carries a
// single range, so the one that is written has to satisfy all of them: it is
// the largest declared, not the last declared.
//
// That distinction was a live trap. Both mesh passes wrote the range in their
// Setup and the last writer won, so correctness rested on feature registration
// order -- the shadow pass asking for 64 bytes had to register before the
// forward pass asking for 5712, in both the game and the editor, each holding
// the rule in a comment. Reversing either one would have left a 64-byte range
// under a shader declaring the larger block. Declaring a minimum instead of
// assigning a value makes the order stop mattering.
//
// Vulkan-free so the rule can be tested without a device, and so the budget is
// visible to the render layer that has to live inside it.
//=============================================================================

// The recorded budget (docs/renderer/constraints.md). Not a hardware limit --
// the guaranteed minimum maxUniformBufferRange is far larger -- but a design
// line: past this, per-frame data belongs in a storage buffer rather than
// growing the block every pass pays to bind.
inline constexpr std::uint64_t kFrameUniformBudgetBytes = 8ull * 1024;

// The range the descriptor should carry given what it already covers and what
// a pass now requires. Monotonic: a pass declaring a small block never shrinks
// the descriptor out from under a pass that declared a larger one.
[[nodiscard]] constexpr std::uint64_t ResolveFrameUniformRange(std::uint64_t current,
                                                               std::uint64_t required)
{
    return required > current ? required : current;
}

// Whether a required range has left the budget behind. Reported rather than
// clamped: clamping would hand a shader a range shorter than the block it
// declares, which is the failure this whole mechanism exists to prevent.
[[nodiscard]] constexpr bool FrameUniformRangeExceedsBudget(std::uint64_t required)
{
    return required > kFrameUniformBudgetBytes;
}
