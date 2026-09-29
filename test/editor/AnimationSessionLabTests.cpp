// The session lab runs an example rig on an authority and a client joined by
// snapshot bytes over a delayed, lossy link. A client that joins late, loses
// snapshots or mispredicts must converge on the authority and stay there.

#include "authoring/AnimationScenario.h"
#include "authoring/AnimationSessionLab.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace
{
    struct Lab
    {
        std::filesystem::path Repo{ SENCHA_REPO_ROOT };
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };
        std::vector<AssetLease> Leases;
        AnimationSessionLab Session{ Assets.DataAssets, &Assets.AnimationClips, {}, &Assets.Skeletons };

        explicit Lab(AnimationLabSettings settings, std::vector<AnimationLabInjection> injections = {})
        {
            for (const std::filesystem::path& root : { Repo / "engine/assets", Repo / "engine/assets/.cooked",
                                                      Repo / "test/fixtures/content/assets",
                                                      Repo / "test/fixtures/content/assets/.cooked" })
                (void)ScanAssetsDirectory(root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
            Leases.push_back(
                Assets.Assets.LoadLease("asset://animation/examples/pump_reload.rig.sdata", AssetType::Data));
            EXPECT_TRUE(Leases.back());
            std::vector<AnimDiagnostic> diagnostics;
            std::optional<AnimationScenario> scenario = LoadAnimationScenario(
                (Repo / "test/fixtures/content/assets/animation/examples/pump_reload.rig.sanimscenario").string(),
                diagnostics);
            EXPECT_TRUE(scenario.has_value());
            EXPECT_TRUE(Session.Open(std::move(*scenario), settings, std::move(injections)));
        }

        ~Lab() { Session.Close(); }

        const AnimationLabTick& At(AnimTick tick) const { return Session.Ticks()[static_cast<std::size_t>(tick)]; }
    };
}

// Joining inside the reload loop over a delaying link that loses a fifth of
// the snapshots: the client finds the loop from the request's anchor and ends
// where the authority is, pose for pose.
TEST(AnimationSessionLab, ALateJoinerOverALossyLinkConvergesOnTheAuthority)
{
    Lab lab({ .JoinTick = 100, .LatencyTicks = 3, .LossPercent = 20 });
    lab.Session.RunTo(280);
    ASSERT_EQ(lab.Session.Ticks().size(), 281u);

    ASSERT_TRUE(lab.Session.JoinedAt().has_value());
    EXPECT_GE(*lab.Session.JoinedAt(), 103u);
    ASSERT_EQ(lab.Session.JoinRequests().size(), 1u);
    EXPECT_NE(lab.Session.JoinRequests().front().AnchorSection, kAnimNoAnchorSection)
        << "a joiner inside a loop needs the anchor to find it";

    std::uint32_t lost = 0;
    for (const AnimationLabTick& tick : lab.Session.Ticks())
        lost += tick.Lost;
    EXPECT_GT(lost, 0u) << "the link must actually lose something for this to prove anything";

    // Agreeing from its first tick with the snapshot, apart from while the
    // fire request is still in transit.
    const AnimTick joined = *lab.Session.JoinedAt();
    EXPECT_TRUE(lab.At(joined).Agrees) << "reconstructed on the joining tick";
    EXPECT_FALSE(lab.At(200).Agrees) << "the authority fires before the client hears of it";
    ASSERT_TRUE(lab.Session.ConvergedAt().has_value());
    EXPECT_LE(*lab.Session.ConvergedAt(), 215u);
    EXPECT_FLOAT_EQ(lab.At(280).Pose.Position, 0.0f);
    EXPECT_FLOAT_EQ(lab.At(280).Pose.Rotation, 0.0f);
    EXPECT_FALSE(lab.At(280).TimingDisagrees);
}

// A client that guesses a fire the authority refuses shows it until the
// refusal arrives, then takes it down and goes back to the reload.
TEST(AnimationSessionLab, ARefusedGuessIsCorrectedWhenTheRefusalArrives)
{
    Lab lab({ .JoinTick = 0, .LatencyTicks = 2 },
            { AnimationLabInjection{ .Tick = 120, .Participant = "player", .Intent = "Anim.Weapon.Fire",
                                     .AuthorityTick = 122, .Confirmed = false } });
    lab.Session.RunTo(119);
    ASSERT_TRUE(lab.At(119).Agrees);
    lab.Session.RunTo(121);
    EXPECT_FALSE(lab.At(121).Agrees) << "the client plays its guess";
    EXPECT_EQ(lab.Session.PendingPredictions(), 1u);

    // Before the scenario's own fire on tick 200.
    lab.Session.RunTo(190);
    EXPECT_EQ(lab.Session.PendingPredictions(), 0u);
    ASSERT_TRUE(lab.Session.ConvergedAt().has_value());
    EXPECT_EQ(*lab.Session.ConvergedAt(), 124u) << "back with the authority the tick its refusal arrived";
    const AnimDecisionRecord* rebuilt = nullptr;
    for (const AnimationPreviewTickRecord& tick : lab.Session.Client().History())
        for (const AnimDecisionRecord& record : tick.Decisions)
            if (record.Reason == AnimChangeReason::Reconstructed)
                rebuilt = &record;
    ASSERT_NE(rebuilt, nullptr);
    EXPECT_EQ(rebuilt->Tick, 124u);
}

// Running back replays both machines from tick 0 over the same link, so a
// run repeats exactly.
TEST(AnimationSessionLab, ARunRepeats)
{
    Lab lab({ .JoinTick = 40, .LatencyTicks = 4, .LossPercent = 30 });
    lab.Session.RunTo(150);
    const std::vector<AnimationLabTick> first = lab.Session.Ticks();
    lab.Session.RunTo(20);
    lab.Session.RunTo(150);
    ASSERT_EQ(lab.Session.Ticks().size(), first.size());
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        EXPECT_EQ(lab.Session.Ticks()[i].Agrees, first[i].Agrees) << i;
        EXPECT_EQ(lab.Session.Ticks()[i].Lost, first[i].Lost) << i;
        EXPECT_EQ(lab.Session.Ticks()[i].Delivered, first[i].Delivered) << i;
    }
    EXPECT_NE(lab.Session.Status().find("joined on tick"), std::string::npos) << lab.Session.Status();
    EXPECT_NE(lab.Session.Status().find("synthetic"), std::string::npos) << lab.Session.Status();
}

