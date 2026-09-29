// Records what scrubbing the animation preview costs -- replaying from tick 0 up
// to the full kept history -- and a scenario batch over the fixture project.
// Env-gated; scripts/bench_animation.sh runs it from the profile preset.

#include "authoring/AnimationPreviewSession.h"
#include "authoring/AnimationScenarioBatch.h"

#include "BenchRecorder.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <format>

TEST(AnimationPreviewBench, Generate)
{
    const char* out = std::getenv("SENCHA_ANIMATION_PREVIEW_BENCH_OUT");
    if (out == nullptr)
        GTEST_SKIP() << "set SENCHA_ANIMATION_PREVIEW_BENCH_OUT to record the preview bench";

    const std::filesystem::path repo(SENCHA_REPO_ROOT);
    const std::filesystem::path content = repo / "test/fixtures/content/assets";
    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets(logging, serializers);
    for (const std::filesystem::path& root : { repo / "engine/assets", repo / "engine/assets/.cooked", content,
                                              content / ".cooked" })
        (void)ScanAssetsDirectory(root.generic_string(), assets.Registry, assets.Assets.Kinds());
    const AssetLease rig = assets.Assets.LoadLease("asset://animation/hero.rig.sdata", AssetType::Data);
    ASSERT_TRUE(rig);
    std::vector<AnimDiagnostic> diagnostics;
    const std::optional<AnimationScenario> scenario =
        LoadAnimationScenario((content / "animation/hero.rig.sanimscenario").string(), diagnostics);
    ASSERT_TRUE(scenario.has_value());

    Bench::Recorder recorder;
    const int reps = Bench::RepsFromEnvironment("SENCHA_ANIMATION_PREVIEW_BENCH_REPS", 20);
    AnimationPreviewSession session(assets.DataAssets, &assets.AnimationClips);
    ASSERT_TRUE(session.Open(*scenario));
    for (const AnimTick to : { AnimTick{ 600 }, AnimTick{ AnimationPreviewSession::kHistoryCapacity } })
    {
        session.RunTo(to);
        std::vector<double> samples;
        for (int i = 0; i < reps; ++i)
        {
            const auto start = Bench::Clock::now();
            session.RunTo(0);
            session.RunTo(to);
            samples.push_back(Bench::MillisecondsSince(start));
        }
        ASSERT_EQ(session.Tick(), to);
        recorder.Record(std::format("anim_preview.replay_from_0_to_{}", to), "ms", Bench::Median(samples));
    }

    AnimationPreviewSession batch(assets.DataAssets, &assets.AnimationClips);
    std::vector<double> samples;
    std::size_t scenarios = 0;
    for (int i = 0; i < reps; ++i)
    {
        const auto start = Bench::Clock::now();
        scenarios = RunAnimationScenarios(batch, assets.Assets, content, {}).size();
        samples.push_back(Bench::MillisecondsSince(start));
    }
    recorder.Record("anim_preview.scenario_batch.scenarios", "count", static_cast<double>(scenarios));
    recorder.Record("anim_preview.scenario_batch", "ms", Bench::Median(samples));

    const std::filesystem::path json(out);
    if (json.has_parent_path())
        std::filesystem::create_directories(json.parent_path());
    ASSERT_TRUE(recorder.WriteJson(json)) << "cannot write " << json.generic_string();
    std::filesystem::path csv = json;
    csv.replace_extension(".csv");
    ASSERT_TRUE(recorder.WriteCsv(csv)) << "cannot write " << csv.generic_string();
}
