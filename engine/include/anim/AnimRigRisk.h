#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct AnimBoundRig;
class GameplayTagRegistry;

//=============================================================================
// Animation content risk
//
// Leading indicators of a rig drifting from stateless selection toward a
// graph: pairwise blend overrides, how many rules selection weighs, flows long
// enough to be a state machine, intents nothing plays, and behaviors that
// both a fact and a request can select. None is an error; each is a reason to
// look. Measured from the bound rig, so it is the same in the editor and the
// game.
//=============================================================================

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
