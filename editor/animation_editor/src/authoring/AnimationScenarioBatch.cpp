#include "authoring/AnimationScenarioBatch.h"

#include "authoring/AnimationPreviewSession.h"

#include <anim/AnimRequestReport.h>
#include <anim/AnimRigBinding.h>
#include <assets/runtime/AssetSystem.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <deque>
#include <format>

namespace
{
    std::vector<std::string> EndingOf(const AnimationPreviewSession& session)
    {
        std::vector<std::string> ending;
        const AnimBoundRig* rig = session.Rig();
        if (rig == nullptr || session.History().empty())
            return ending;
        const GameplayTagRegistry* tags = session.Tags();
        const AnimationPreviewTickRecord& last = session.History().back();
        for (std::size_t l = 0; l < last.Layers.size() && l < rig->Layers.size(); ++l)
        {
            const AnimationPreviewLayerRecord& layer = last.Layers[l];
            const std::string behavior =
                tags != nullptr && layer.Behavior.IsValid() ? std::string(tags->GetName(layer.Behavior)) : "(none)";
            const std::string content =
                layer.Content < rig->Contents.size() ? rig->Contents[layer.Content].Path : std::string("(none)");
            ending.push_back(std::format("{}: {} playing {}", rig->Layers[l].NameText, behavior, content));
        }
        return ending;
    }

    std::uint32_t UnplayedIn(const AnimationPreviewSession& session)
    {
        const World* world = session.SimulationWorld();
        const EntityId subject = session.Subject();
        if (world == nullptr || !subject.IsValid() || !world->IsRegistered<AnimRequestReport>())
            return 0;
        const AnimRequestReport* report = world->TryGet<AnimRequestReport>(subject);
        return report != nullptr ? report->Unplayed : 0;
    }
}

AnimTick AnimationScenarioRunLength(const AnimationScenario& scenario)
{
    const AnimTick lastAction = scenario.Actions.empty() ? 0 : scenario.Actions.back().Tick;
    return lastAction + static_cast<AnimTick>(scenario.TickRate) * 2;
}

std::vector<std::filesystem::path> FindAnimationScenarios(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> found;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(root, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
    {
        if (it->is_directory() && it->path().filename() == ".cooked")
        {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file() && it->path().extension() == ".sanimscenario")
            found.push_back(it->path());
    }
    std::ranges::sort(found);
    return found;
}

AnimationScenarioRun RunAnimationScenario(AnimationPreviewSession& session, AnimationScenario scenario,
                                          std::vector<AnimDiagnostic> loadProblems)
{
    AnimationScenarioRun run;
    run.Name = scenario.Name;
    run.RigPath = scenario.RigPath;
    run.LastTick = AnimationScenarioRunLength(scenario);

    run.Ran = session.Open(std::move(scenario));
    run.Problems = session.Problems();
    if (run.Ran)
    {
        session.RunTo(run.LastTick);
        const std::deque<AnimationPreviewTickRecord> first = session.History();
        run.UnplayedRequests = UnplayedIn(session);
        run.Ending = EndingOf(session);
        run.Problems = session.Problems();

        session.Restart();
        session.RunTo(run.LastTick);
        const std::deque<AnimationPreviewTickRecord>& second = session.History();
        run.Reproduces = first.size() == second.size()
            && std::ranges::equal(first, second, SameAnimationPreviewTick);
    }
    session.Close();
    run.Problems.insert(run.Problems.begin(), loadProblems.begin(), loadProblems.end());

    const bool errors = std::ranges::any_of(run.Problems, [](const AnimDiagnostic& problem) {
        return problem.Severity == AnimDiagnosticSeverity::Error;
    });
    run.Verdict = !run.Ran || errors || !run.Reproduces ? AnimationScenarioVerdict::Failed
                : !run.Problems.empty() || run.UnplayedRequests > 0 ? AnimationScenarioVerdict::Warned
                                                                     : AnimationScenarioVerdict::Passed;
    return run;
}

std::vector<AnimationScenarioRun> RunAnimationScenarios(AnimationPreviewSession& session, AssetSystem& assets,
                                                        const std::filesystem::path& root,
                                                        const std::string& rigOverride)
{
    std::vector<AnimationScenarioRun> runs;
    for (const std::filesystem::path& file : FindAnimationScenarios(root))
    {
        std::vector<AnimDiagnostic> problems;
        std::optional<AnimationScenario> scenario = LoadAnimationScenario(file.string(), problems);
        AnimationScenarioRun run;
        if (scenario)
        {
            if (!rigOverride.empty())
                scenario->RigPath = rigOverride;
            const AssetLease rig = assets.LoadLease(scenario->RigPath, AssetType::Data);
            run = RunAnimationScenario(session, std::move(*scenario), std::move(problems));
        }
        else
        {
            run.Problems = std::move(problems);
        }
        run.File = std::filesystem::relative(file, root).generic_string();
        runs.push_back(std::move(run));
    }
    return runs;
}
