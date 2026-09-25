#pragma once

#include "authoring/AnimationScenario.h"

#include <anim/AnimDiagnostic.h>
#include <anim/AnimTypes.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class AnimationPreviewSession;

enum class AnimationScenarioVerdict : std::uint8_t
{
    Passed,
    Warned,
    Failed,
};

struct AnimationScenarioRun
{
    // Relative to the authoring root, with forward slashes.
    std::string File;
    std::string Name;
    std::string RigPath;
    AnimTick LastTick = 0;
    std::vector<AnimDiagnostic> Problems;
    std::uint32_t UnplayedRequests = 0;
    // False when the scenario's rig could not be opened.
    bool Ran = false;
    // A second run from tick 0 matched the first on every tick both kept.
    bool Reproduces = false;
    // Each layer's behavior and content on the last tick.
    std::vector<std::string> Ending;
    AnimationScenarioVerdict Verdict = AnimationScenarioVerdict::Failed;
};

// Two seconds of the scenario's clock past its last action, so what the last
// action started has time to play out.
[[nodiscard]] AnimTick AnimationScenarioRunLength(const AnimationScenario& scenario);

// Every scenario sidecar under `root`, sorted, skipping cooked output.
[[nodiscard]] std::vector<std::filesystem::path> FindAnimationScenarios(const std::filesystem::path& root);

// Runs `scenario` in `session` twice from tick 0 and judges it, with what
// reading its file found: failed on an error or a run that does not repeat,
// warned on a warning or a request no layer played. Leaves the session closed.
[[nodiscard]] AnimationScenarioRun RunAnimationScenario(AnimationPreviewSession& session,
                                                        AnimationScenario scenario,
                                                        std::vector<AnimDiagnostic> loadProblems = {});
