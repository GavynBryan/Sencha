#include "authoring/AnimationScenarioBatchProperties.h"

#include "authoring/AnimationPreviewWorkspace.h"

#include "properties/PropertyTree.h"
#include "properties/SettingProperties.h"

#include <format>

namespace
{
constexpr std::string_view kRuns = "runs.";

std::string_view VerdictName(AnimationScenarioVerdict verdict)
{
    switch (verdict)
    {
    case AnimationScenarioVerdict::Passed: return "Passed";
    case AnimationScenarioVerdict::Warned: return "Warned";
    case AnimationScenarioVerdict::Failed: return "Failed";
    }
    return "?";
}
} // namespace

std::vector<PropertyNode> AnimationScenarioBatchProperties::Nodes() const
{
    PropertyNode run = NoteProperty("run", "Runs",
                                    "Every saved scenario twice from the start, to two seconds past its last action. "
                                    "It fails on an error or a second run that differs from the first, and warns on a "
                                    "warning or a request nothing played.");
    run.Descriptor.Verbs = { { .Id = "all", .Label = "Run All" } };
    if (!Workspace.Rig.Path.empty())
        run.Descriptor.Verbs.push_back({ .Id = "rig", .Label = "Run All Against the Open Rig" });
    std::vector<PropertyNode> nodes{ std::move(run) };
    if (Runs.empty())
    {
        nodes.push_back(NoteProperty("runs.none", "Results", "None yet."));
        return nodes;
    }
    for (std::size_t r = 0; r < Runs.size(); ++r)
    {
        const AnimationScenarioRun& result = Runs[r];
        PropertyNode group = GroupProperty(std::format("{}{}", kRuns, r),
                                           std::format("{}: {}", VerdictName(result.Verdict), result.File));
        if (!result.RigPath.empty())
        {
            group.Children.push_back(NoteProperty(std::format("{}{}.rig", kRuns, r), "Rig", result.RigPath));
            group.Descriptor.Verbs = { { .Id = "open", .Label = "Open Rig" } };
        }
        std::string ending;
        for (const std::string& layer : result.Ending)
            ending += (ending.empty() ? "" : "; ") + layer;
        if (!ending.empty())
            group.Children.push_back(NoteProperty(std::format("{}{}.ending", kRuns, r), "Ended on", ending));
        if (result.Ran && !result.Reproduces)
            group.Children.push_back(NoteProperty(std::format("{}{}.replay", kRuns, r), "Replay",
                                                  "A second run from tick 0 did not match the first."));
        if (result.UnplayedRequests > 0)
            group.Children.push_back(NoteProperty(std::format("{}{}.unplayed", kRuns, r), "Unplayed",
                                                  std::format("{} requests ended with no layer playing them.",
                                                              result.UnplayedRequests)));
        for (std::size_t p = 0; p < result.Problems.size(); ++p)
            group.Children.push_back(NoteProperty(std::format("{}{}.problems.{}", kRuns, r, p), "Problem",
                                                  FormatAnimDiagnostic(result.Problems[p])));
        nodes.push_back(std::move(group));
    }
    return nodes;
}

PropertyEditResult AnimationScenarioBatchProperties::Act(std::string_view key, std::string_view verb,
                                                         std::string_view)
{
    if (key == "run" && (verb == "all" || verb == "rig"))
    {
        Runs = Workspace.RunScenarioBatch(verb == "rig");
        return PropertyEditResult::Applied;
    }
    const std::optional<PropertyKeyIndex> run = ParsePropertyKeyIndex(key, kRuns);
    if (verb != "open" || !run || run->Index >= Runs.size())
        return PropertyEditResult::Gone;
    OpenRig(Runs[run->Index].RigPath);
    return PropertyEditResult::Applied;
}
