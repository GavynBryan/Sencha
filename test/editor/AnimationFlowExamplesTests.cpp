// The six flow shapes the architecture was tested against, as content an
// author opens and runs: each rig under the scenario saved beside it, loaded
// through the runtime asset pipeline, plays its sections in the order and on
// the ticks the flow rules say.

#include "authoring/AnimationPreviewSession.h"
#include "authoring/AnimationScenario.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace
{
    struct Examples
    {
        std::filesystem::path Repo{ SENCHA_REPO_ROOT };
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };
        std::vector<AssetLease> Leases;

        Examples()
        {
            for (const std::filesystem::path& root : { Repo / "engine/assets", Repo / "engine/assets/.cooked",
                                                      Repo / "test/fixtures/content/assets",
                                                      Repo / "test/fixtures/content/assets/.cooked" })
                (void)ScanAssetsDirectory(root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        }

        // What an example did by the tick it ran to.
        struct Outcome
        {
            // Every section change, as "tick section reason".
            std::vector<std::string> Sections;
            // Per layer: the behavior playing and its weight.
            std::vector<std::string> Behaviors;
            std::vector<float> Weights;
            std::vector<bool> Masked;
        };

        Outcome Run(const std::string& name, AnimTick last)
        {
            Outcome outcome;
            const std::string rig = std::format("asset://animation/examples/{}.rig.sdata", name);
            Leases.push_back(Assets.Assets.LoadLease(rig, AssetType::Data));
            EXPECT_TRUE(Leases.back()) << rig;
            std::vector<AnimDiagnostic> diagnostics;
            std::optional<AnimationScenario> scenario = LoadAnimationScenario(
                (Repo / std::format("test/fixtures/content/assets/animation/examples/{}.rig.sanimscenario", name))
                    .string(),
                diagnostics);
            EXPECT_TRUE(scenario.has_value()) << name;
            EXPECT_TRUE(diagnostics.empty()) << FormatAnimDiagnostic(diagnostics.front());
            if (!scenario)
                return outcome;

            AnimationPreviewSession session(Assets.DataAssets, &Assets.AnimationClips, {}, &Assets.Skeletons);
            EXPECT_TRUE(session.Open(std::move(*scenario)));
            const AnimBoundRig* bound = session.Rig();
            EXPECT_NE(bound, nullptr);
            if (bound == nullptr)
                return outcome;
            EXPECT_TRUE(bound->Valid) << (bound->Diagnostics.empty() ? std::string()
                                                                     : FormatAnimDiagnostic(bound->Diagnostics.front()));
            session.RunTo(last);
            EXPECT_TRUE(session.ScenarioProblems().empty())
                << FormatAnimDiagnostic(session.ScenarioProblems().front());

            bound = session.Rig();
            for (const AnimationPreviewTickRecord& tick : session.History())
                for (const AnimDecisionRecord& record : tick.Decisions)
                {
                    if (record.Cause != AnimDecisionCause::SectionChanged || record.Content >= bound->Contents.size()
                        || bound->Contents[record.Content].Flow < 0)
                        continue;
                    const AnimBoundFlow& flow = bound->Flows[static_cast<std::size_t>(bound->Contents[record.Content].Flow)];
                    outcome.Sections.push_back(std::format("{} {} {}", record.Tick,
                                                           flow.Sections[record.Section].TagName,
                                                           AnimChangeReasonName(record.Reason)));
                }
            for (const AnimationPreviewLayerRecord& layer : session.History().back().Layers)
            {
                outcome.Behaviors.emplace_back(session.Tags()->GetName(layer.Behavior));
                outcome.Weights.push_back(layer.Weight);
            }
            for (const AnimBoundLayer& layer : bound->Layers)
                outcome.Masked.push_back(layer.Masked());
            session.Close();
            return outcome;
        }
    };

    using Lines = std::vector<std::string>;
}

TEST(AnimationFlowExamples, PumpReloadInsertsUntilFireCancelsAtOnce)
{
    // Inserts loop while the magazine is not full and the reload is held;
    // fire interrupts, and the insert's immediate cancel goes to the exit at
    // once. Fire plays when the exit completes.
    Examples examples;
    const Examples::Outcome outcome = examples.Run("pump_reload", 280);
    EXPECT_EQ(outcome.Sections, (Lines{ "10 Anim.Reload.Enter FlowStarted", "70 Anim.Reload.Insert SectionFollowed",
                                        "130 Anim.Reload.Insert SectionLooped", "190 Anim.Reload.Insert SectionLooped",
                                        "200 Anim.Reload.Exit SectionCancelled" }));
    EXPECT_EQ(outcome.Behaviors, (Lines{ "Anim.Weapon.Fire" }));
}

TEST(AnimationFlowExamples, MeleeComboAdvancesBySupersedingItsRequest)
{
    // Gameplay answers the first swing's window with a superseding request
    // naming the next combo step; the slot map resolves the second swing.
    Examples examples;
    const Examples::Outcome outcome = examples.Run("melee_combo", 420);
    EXPECT_EQ(outcome.Sections,
              (Lines{ "10 Anim.Melee.First.Swing FlowStarted", "70 Anim.Melee.First.Window SectionFollowed",
                      "80 Anim.Melee.Second.Swing FlowStarted", "140 Anim.Melee.Second.Window SectionFollowed",
                      "200 Anim.Melee.Second.Recover SectionFollowed" }));
    EXPECT_EQ(outcome.Behaviors, (Lines{ "Anim.Locomotion.Idle" }));
}

TEST(AnimationFlowExamples, ChargeHoldsWhileHeldAndReleasesByParameter)
{
    // The hold repeats while the request is held; released, the flow plays
    // the release variant its charge parameter picks, then recovers.
    Examples examples;
    const Examples::Outcome outcome = examples.Run("charge_attack", 400);
    EXPECT_EQ(outcome.Sections, (Lines{ "10 Anim.Charge.Windup FlowStarted", "70 Anim.Charge.Hold SectionFollowed",
                                        "130 Anim.Charge.Hold SectionLooped", "190 Anim.Charge.Hold SectionLooped",
                                        "250 Anim.Charge.Release SectionFollowed",
                                        "310 Anim.Charge.Recover SectionFollowed" }));
    EXPECT_EQ(outcome.Behaviors, (Lines{ "Anim.Locomotion.Idle" }));
}

TEST(AnimationFlowExamples, AFailedLedgeClimbDropsThroughTheCancelSection)
{
    // Failed during the pull: the pull finishes, then the drop, never the
    // mantle.
    Examples examples;
    const Examples::Outcome outcome = examples.Run("ledge_climb", 200);
    EXPECT_EQ(outcome.Sections, (Lines{ "10 Anim.Ledge.Grab FlowStarted", "70 Anim.Ledge.Pull SectionFollowed",
                                        "130 Anim.Ledge.Drop SectionCancelled" }));
    EXPECT_EQ(outcome.Behaviors, (Lines{ "Anim.Locomotion.Idle" }));
}

TEST(AnimationFlowExamples, DrawAndFirePlayOnTheUpperBodyWhileWalking)
{
    // The draw flow and then fire play on the masked upper layer, weighted in
    // by a weight rule, while the base layer keeps walking.
    Examples examples;
    const Examples::Outcome outcome = examples.Run("draw_fire", 180);
    EXPECT_EQ(outcome.Sections,
              (Lines{ "10 Anim.Draw.Unholster FlowStarted", "70 Anim.Draw.Ready SectionFollowed" }));
    EXPECT_EQ(outcome.Behaviors, (Lines{ "Anim.Locomotion.Walk", "Anim.Weapon.Fire" }));
    EXPECT_EQ(outcome.Weights, (std::vector<float>{ 1.0f, 1.0f }));
    EXPECT_EQ(outcome.Masked, (std::vector<bool>{ false, true }));

    const Examples::Outcome lowered = examples.Run("draw_fire", 260);
    EXPECT_EQ(lowered.Behaviors, (Lines{ "Anim.Locomotion.Walk", "Anim.Weapon.Lowered" }));
    EXPECT_EQ(lowered.Weights, (std::vector<float>{ 1.0f, 0.0f }));
}

TEST(AnimationFlowExamples, TheDoorSwingsOnARequestAndRestsOnAFact)
{
    // A fixed request swings it; the rested pose is the DoorOpen fact's, so a
    // late joiner needs only the fact.
    Examples examples;
    EXPECT_EQ(examples.Run("door", 40).Behaviors, (Lines{ "Anim.Door.Opening" }));
    const Examples::Outcome outcome = examples.Run("door", 120);
    EXPECT_EQ(outcome.Sections, (Lines{ "10 Anim.Door.Swing FlowStarted" }));
    EXPECT_EQ(outcome.Behaviors, (Lines{ "Anim.Door.Opened" }));
}
