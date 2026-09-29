#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct AnimBoundRig;
class GameplayTagRegistry;

// Signs a rig is drifting toward a graph, measured from the bound rig so the
// editor and the game agree. See docs/gameplay/animation.md.

struct AnimRigRiskFinding
{
    // Stable, dotted rule identifier.
    std::string Rule;
    std::string Message;
};

struct AnimRigRisk
{
    std::uint32_t BlendOverrides = 0;
    std::uint32_t SelectorRules = 0;
    // The most rules one layer's selector weighs, delegation flattened.
    std::uint32_t DeepestSelector = 0;
    std::uint32_t LongFlows = 0;
    std::vector<AnimRigRiskFinding> Findings;
};

// Flows with more sections than this are reported.
inline constexpr std::uint32_t kAnimRiskFlowSections = 8;
// More blend overrides than this are reported (the cook warns at the same).
inline constexpr std::uint32_t kAnimRiskBlendOverrides = 8;

[[nodiscard]] AnimRigRisk MeasureAnimRigRisk(const AnimBoundRig& rig, const GameplayTagRegistry* tags);
