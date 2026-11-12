// Records rig binding per tier, a headless server tick over cosmetic props with
// and without the participation skip, and the pose pass serially and across
// workers. Env-gated; scripts/bench_animation.sh runs it from the profile preset.

#include "AnimCrowdFixture.h"
#include "BenchRecorder.h"

#include <anim/AnimEventSystem.h>
#include <anim/AnimPoseSystem.h>
#include <jobs/JobSystem.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <string>

namespace
{
    Bench::Recorder Recorder;

    int Reps(int fallback) { return Bench::RepsFromEnvironment("SENCHA_ANIM_BENCH_REPS", fallback); }

    // One layer idling on one clip, with no gameplay reach: what a server skips.
    DataAssetHandle LoadProp(AnimRigFixture& fx)
    {
        (void)fx.Tags().RegisterTag("Anim.Prop.Spin");
        fx.Clip("asset://bench/spin.sanim", 1.0f);
        (void)fx.Load("asset://bench/prop.behaviors.sdata", kAnimBehaviorSetType,
                      R"({ "behaviors": [ { "tag": "Anim.Prop.Spin", "kind": "cyclic" } ] })");
        (void)fx.Load("asset://bench/prop.slots.sdata", kAnimSlotMapType,
                      R"({ "rows": [ { "id": "spin", "behavior": "Anim.Prop.Spin", "clip": "asset://bench/spin.sanim" } ] })");
        return fx.Load("asset://bench/prop.rig.sdata", kAnimRigType, R"({
            "behaviors": [ "asset://bench/prop.behaviors.sdata" ], "slot_maps": [ "asset://bench/prop.slots.sdata" ],
            "layers": [ { "name": "anim.layer.base", "idle": "Anim.Prop.Spin" } ] })");
    }

    double MedianBindMs(const AnimRigFixture& fx, DataAssetHandle rig, int reps)
    {
        std::vector<double> samples;
        for (int i = 0; i < reps; ++i)
        {
            const auto start = Bench::Clock::now();
            const AnimBoundRig bound = BindAnimRig(fx.Data, &fx.Clips, &fx.Skeletons, rig, fx.Entities);
            samples.push_back(Bench::MillisecondsSince(start));
            EXPECT_TRUE(bound.Valid) << AnimRigFixture::Describe(bound);
        }
        return Bench::Median(samples);
    }

    void MeasureBinding()
    {
        AnimCrowd::Fixture fx;
        AnimCharacterRig::RegisterTags(fx);
        const DataAssetHandle character = AnimCharacterRig::Load(fx);
        const DataAssetHandle prop = LoadProp(fx);
        const int reps = Reps(200);
        Recorder.Record("anim.bind.prop", "ms", MedianBindMs(fx, prop, reps));
        Recorder.Record("anim.bind.simple_two_layer", "ms", MedianBindMs(fx, fx.Rig, reps));
        Recorder.Record("anim.bind.character", "ms", MedianBindMs(fx, character, reps));
    }

    // Median milliseconds for one fixed tick of logic over `props` cosmetic props.
    double MedianServerTickMs(std::size_t props, bool presentsPose, int reps)
    {
        AnimRigFixture fx;
        const DataAssetHandle rig = LoadProp(fx);
        for (std::size_t i = 0; i < props; ++i)
            (void)fx.Character(rig);
        AnimRigCompositionSystem composition(presentsPose);
        AnimFactGatherSystem gather(presentsPose);
        AnimSelectSystem select(presentsPose);
        AnimContentSystem content(presentsPose);
        AnimEventSystem events(nullptr, presentsPose);
        std::vector<double> samples;
        for (int tick = 0; tick < 60 + reps; ++tick, ++fx.Now)
        {
            const auto start = Bench::Clock::now();
            composition.Compose(fx.Entities);
            gather.Gather(fx.Entities, fx.Now, AnimRigFixture::kTick);
            select.Select(fx.Entities, fx.Now, AnimRigFixture::kTick);
            content.Resolve(fx.Entities, fx.Now, AnimRigFixture::kTick);
            events.Run(fx.Entities, fx.Now, AnimRigFixture::kTick);
            if (tick >= 60)
                samples.push_back(Bench::MillisecondsSince(start));
        }
        return Bench::Median(samples);
    }

    void MeasureServerTick()
    {
        for (const std::size_t props : { std::size_t{ 1 }, std::size_t{ 64 }, std::size_t{ 1024 } })
        {
            Recorder.Record(std::format("anim.server_tick.props_{}.skipped", props), "ms",
                            MedianServerTickMs(props, false, Reps(240)));
            Recorder.Record(std::format("anim.server_tick.props_{}.run", props), "ms",
                            MedianServerTickMs(props, true, Reps(240)));
        }
    }

    // The crowd's rig with `layers` layers: its base, then additive layers over part of the body.
    DataAssetHandle LoadLayeredRig(AnimCrowd::Fixture& crowd, int layers)
    {
        std::string json = R"({ "name": "anim.layer.base", "selector": "asset://bench/selector.sdata", "idle": "Anim.Idle" })";
        for (int layer = 1; layer < layers; ++layer)
        {
            const std::string name = std::format("anim.layer.bench{}", layer);
            (void)crowd.Tags().RegisterTag(name);
            json += std::format(R"(, {{ "name": "{}", "selector": "asset://bench/selector.sdata", "idle": "Anim.Idle",
                                       "mode": "additive", "weight": 0.5, "mask": [ {{ "joint": "j1" }} ] }})", name);
        }
        return crowd.Load(std::format("asset://bench/rig_{}.sdata", layers), kAnimRigType, std::format(R"({{
            "skeleton": "asset://bench/crowd.sskel", "facts": "asset://bench/facts.sdata",
            "behaviors": [ "asset://bench/behaviors.sdata" ], "slot_maps": [ "asset://bench/slots.sdata" ],
            "layers": [ {} ] }})", json));
    }

    // Median milliseconds per pose pass over a crowd, after the blends settle.
    double MedianPoseMs(std::size_t characters, std::uint32_t workers, int reps, int layers = 2, int period = 40)
    {
        AnimCrowd::Fixture crowd;
        const DataAssetHandle rig = layers == 2 ? crowd.Rig : LoadLayeredRig(crowd, layers);
        for (std::size_t i = 0; i < characters; ++i)
            crowd.Characters.push_back(crowd.DrawnCharacter(rig));
        std::unique_ptr<JobSystem> jobs = workers > 0 ? std::make_unique<JobSystem>(workers) : nullptr;
        AnimPoseSystem poser(jobs.get());
        std::vector<double> samples;
        for (int tick = 0; tick < 60 + reps; ++tick)
        {
            for (std::size_t i = 0; i < crowd.Characters.size(); ++i)
                crowd.Motion(crowd.Characters[i]).Speed =
                    ((tick + static_cast<int>(i) * 7) / period) % 2 == 0 ? 0.0f : 1.0f;
            crowd.Tick();
            const auto start = Bench::Clock::now();
            poser.Pose(crowd.Entities, crowd.Last(), AnimRigFixture::kTick);
            if (tick >= 60)
                samples.push_back(Bench::MillisecondsSince(start));
        }
        return Bench::Median(samples);
    }

    void MeasurePose()
    {
        for (const std::size_t characters : { std::size_t{ 1 }, std::size_t{ 64 }, std::size_t{ 256 } })
            for (const std::uint32_t workers : { 0u, 3u, 7u })
                Recorder.Record(std::format("anim.pose.characters_{}.workers_{}", characters, workers), "ms",
                                MedianPoseMs(characters, workers, Reps(180)));
        for (const int layers : { 1, 2, 3, 4, 8 })
            for (const std::size_t characters : { std::size_t{ 64 }, std::size_t{ 256 } })
                Recorder.Record(std::format("anim.pose.layers_{}.characters_{}", layers, characters), "ms",
                                MedianPoseMs(characters, 0, Reps(180), layers));
        for (const int period : { 40, 12, 4 })
            Recorder.Record(std::format("anim.pose.transition_period_{}.characters_64", period), "ms",
                            MedianPoseMs(64, 0, Reps(180), 2, period));
    }
}

TEST(AnimBench, Generate)
{
    const char* out = std::getenv("SENCHA_ANIM_BENCH_OUT");
    if (out == nullptr)
        GTEST_SKIP() << "set SENCHA_ANIM_BENCH_OUT to record the animation bench (use scripts/bench_animation.sh)";
    const std::filesystem::path json(out);
    if (json.has_parent_path())
        std::filesystem::create_directories(json.parent_path());

    Recorder.Clear();
    MeasureBinding();
    MeasureServerTick();
    MeasurePose();

    ASSERT_TRUE(Recorder.WriteJson(json)) << "cannot write " << json.generic_string();
    std::filesystem::path csv = json;
    csv.replace_extension(".csv");
    ASSERT_TRUE(Recorder.WriteCsv(csv)) << "cannot write " << csv.generic_string();
}
