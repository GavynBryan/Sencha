// Every saved scenario in a project run and judged, without touching the
// scenario being worked on.

#include "EditorDocumentsFixture.h"
#include "authoring/AnimationPreviewWorkspace.h"
#include "authoring/AnimationRigRecipe.h"
#include "authoring/AnimationScenarioBatch.h"

#include "AnimationTestProject.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

namespace
{
    constexpr const char* kSkeleton = "asset://meshes/man.blend#skel:Man";

    struct Project : AnimationTestProject
    {
        Project() : AnimationTestProject("sencha_scenario_batch")
        {
            Skeleton(kSkeleton);
            for (const char* clip : { "asset://meshes/man.blend#anim:Idle", "asset://meshes/man.blend#anim:Wave",
                                      "asset://meshes/man.blend#anim:Bow" })
                Clip(clip, kSkeleton);
        }
    };

    AnimationRigRecipe Prop(const char* name, const char* clip)
    {
        return { .Name = name, .Clips = { "asset://meshes/man.blend#anim:Idle", clip },
                 .Preset = AnimationRigPreset::Prop, .UpperBodyJoint = {} };
    }

    const AnimationScenarioRun& RunOf(const std::vector<AnimationScenarioRun>& runs, std::string_view file)
    {
        const auto found = std::ranges::find(runs, file, &AnimationScenarioRun::File);
        EXPECT_NE(found, runs.end()) << file;
        return *found;
    }
}

TEST(AnimationScenarioBatch, EveryScenarioIsRunAndJudged)
{
    Project project;
    EditorDocuments documents(*project.Assets);
    AnimationPreviewWorkspace workspace(*project.Assets, documents.Sources, documents.Store, {}, project.Root);
    std::string error;
    ASSERT_TRUE(workspace.CreateRig(Prop("waver", "asset://meshes/man.blend#anim:Wave"), error)) << error;
    ASSERT_TRUE(workspace.CreateRig(Prop("bower", "asset://meshes/man.blend#anim:Bow"), error)) << error;

    // A scenario setting a fact its rig does not have, and one naming a rig
    // that is not there.
    AnimationScenario unknownFact;
    unknownFact.Name = "unknown_fact";
    unknownFact.RigPath = "asset://animation/waver/waver.rig.sdata";
    unknownFact.Participants = { "player" };
    AnimationScenarioAction set;
    set.Tick = 5;
    set.Kind = AnimationScenarioActionKind::SetFact;
    set.Fact = "Nope";
    set.Value = AnimationScenarioValue::FromBool(true);
    unknownFact.Append(set);
    std::filesystem::create_directories(project.Root / "extra");
    ASSERT_TRUE(SaveAnimationScenario(unknownFact, (project.Root / "extra/unknown_fact.sanimscenario").string(), error))
        << error;
    AnimationScenario missingRig = unknownFact;
    missingRig.Name = "missing_rig";
    missingRig.RigPath = "asset://animation/nobody.rig.sdata";
    missingRig.Actions.clear();
    ASSERT_TRUE(SaveAnimationScenario(missingRig, (project.Root / "extra/missing_rig.sanimscenario").string(), error))
        << error;

    const std::vector<AnimationScenarioRun> runs = workspace.RunScenarioBatch(false);
    ASSERT_EQ(runs.size(), 4u);

    const AnimationScenarioRun& waver = RunOf(runs, "animation/waver/waver.rig.sanimscenario");
    EXPECT_EQ(waver.Verdict, AnimationScenarioVerdict::Passed);
    EXPECT_TRUE(waver.Ran);
    EXPECT_TRUE(waver.Reproduces);
    EXPECT_EQ(waver.LastTick, 120u);
    ASSERT_EQ(waver.Ending.size(), 1u);
    EXPECT_NE(waver.Ending[0].find("asset://meshes/man.blend#anim:Idle"), std::string::npos) << waver.Ending[0];

    const AnimationScenarioRun& fact = RunOf(runs, "extra/unknown_fact.sanimscenario");
    EXPECT_EQ(fact.Verdict, AnimationScenarioVerdict::Failed);
    EXPECT_TRUE(fact.Ran) << "it fails for its fact, not its rig";
    EXPECT_TRUE(std::ranges::any_of(fact.Problems, [](const AnimDiagnostic& problem) {
        return problem.Code == "anim.scenario.unknown_fact";
    }));

    const AnimationScenarioRun& missing = RunOf(runs, "extra/missing_rig.sanimscenario");
    EXPECT_EQ(missing.Verdict, AnimationScenarioVerdict::Failed);
    EXPECT_FALSE(missing.Ran);
}

// Against the open rig: every scenario, whichever rig it was saved for, runs
// under the rig being edited -- and the working simulation stays as it was.
TEST(AnimationScenarioBatch, ScenariosRunAgainstTheOpenRigWithoutDisturbingIt)
{
    Project project;
    EditorDocuments documents(*project.Assets);
    AnimationPreviewWorkspace workspace(*project.Assets, documents.Sources, documents.Store, {}, project.Root);
    std::string error;
    ASSERT_TRUE(workspace.CreateRig(Prop("bower", "asset://meshes/man.blend#anim:Bow"), error)) << error;
    ASSERT_TRUE(workspace.CreateRig(Prop("waver", "asset://meshes/man.blend#anim:Wave"), error)) << error;
    ASSERT_EQ(workspace.Rig.Path, "asset://animation/waver/waver.rig.sdata");
    workspace.Rig.Simulation.RunTo(17);

    const std::vector<AnimationScenarioRun> runs = workspace.RunScenarioBatch(true);
    ASSERT_EQ(runs.size(), 2u);
    for (const AnimationScenarioRun& run : runs)
    {
        EXPECT_EQ(run.RigPath, "asset://animation/waver/waver.rig.sdata") << run.File;
        EXPECT_EQ(run.Verdict, AnimationScenarioVerdict::Passed) << run.File;
    }

    ASSERT_TRUE(workspace.Rig.Simulation.IsOpen());
    EXPECT_EQ(workspace.Rig.Simulation.Scenario().RigPath, "asset://animation/waver/waver.rig.sdata");
    EXPECT_EQ(workspace.Rig.Simulation.History().back().Tick, 17u);
}

TEST(AnimationScenarioBatch, AScenarioRunsTwoSecondsPastItsLastAction)
{
    AnimationScenario scenario;
    scenario.TickRate = 30;
    EXPECT_EQ(AnimationScenarioRunLength(scenario), 60u);
    AnimationScenarioAction action;
    action.Tick = 100;
    scenario.Append(action);
    EXPECT_EQ(AnimationScenarioRunLength(scenario), 160u);
}
