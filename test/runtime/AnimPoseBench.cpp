// A measurement of the pose pass, not a test of it: a crowd of 62-joint
// two-layer characters (the shape of the SenchaTest praying man), every joint
// animated, walking and idling with inertialized changes, posed serially and
// across workers. Env-gated like the render benches; run it from a Release
// build:
//
//   SENCHA_ANIM_POSE_BENCH=1 build-profile/test/runtime_tests --gtest_filter='AnimPoseBench.*'

#include "AnimRigFixture.h"

#include <anim/AnimPoseSystem.h>
#include <jobs/JobSystem.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>

namespace
{
    constexpr std::uint32_t kJoints = 62;

    struct Crowd : AnimRigFixture
    {
        DataAssetHandle Rig;
        std::vector<EntityId> Characters;

        Crowd()
        {
            for (const char* tag : { "Anim.Idle", "Anim.Walk", "anim.layer.upper" })
                (void)Tags().RegisterTag(tag);
            SkeletonData skeleton;
            for (std::uint32_t j = 0; j < kJoints; ++j)
            {
                SkeletonJoint joint;
                joint.Name = std::format("j{}", j);
                joint.ParentIndex = j == 0 ? -1 : static_cast<std::int32_t>((j - 1) / 2);
                skeleton.Joints.push_back(joint);
            }
            (void)Skeletons.Register("asset://bench/crowd.sskel", std::move(skeleton));
            // Every joint rotates and translates over eight keys.
            for (const auto& [path, amount] : { std::pair{ "asset://bench/idle.sanim", 0.2f },
                                                std::pair{ "asset://bench/walk.sanim", 0.9f } })
            {
                AnimationClipData clip;
                clip.DurationSeconds = 1.0f;
                clip.SkeletonPath = "asset://bench/crowd.sskel";
                for (std::uint32_t j = 0; j < kJoints; ++j)
                {
                    AnimationJointTrack rotation;
                    rotation.JointIndex = j;
                    rotation.Path = AnimationChannelPath::Rotation;
                    AnimationJointTrack translation;
                    translation.JointIndex = j;
                    translation.Path = AnimationChannelPath::Translation;
                    for (int k = 0; k < 8; ++k)
                    {
                        const float t = static_cast<float>(k) / 7.0f;
                        const Quatf q = Quatf::FromAxisAngle(Vec3d::Up(), amount * std::sin(6.283f * t + j));
                        rotation.TimesSeconds.push_back(t);
                        rotation.Values.insert(rotation.Values.end(), { q.X, q.Y, q.Z, q.W });
                        translation.TimesSeconds.push_back(t);
                        translation.Values.insert(translation.Values.end(), { amount * t, 0.1f * j, 0.0f });
                    }
                    clip.Tracks.push_back(std::move(rotation));
                    clip.Tracks.push_back(std::move(translation));
                }
                (void)Clips.Register(path, std::move(clip), Skeletons.AcquireOwned("asset://bench/crowd.sskel"));
            }
            (void)Load("asset://bench/facts.sdata", kAnimFactSchemaType,
                       R"({ "slots": [ { "name": "Speed", "kind": "float" } ] })");
            (void)Load("asset://bench/behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
                { "tag": "Anim.Idle", "kind": "cyclic", "blend": { "in": "inertialize", "in_ms": 200 } },
                { "tag": "Anim.Walk", "kind": "cyclic", "blend": { "in": "inertialize", "in_ms": 200 } } ] })");
            (void)Load("asset://bench/selector.sdata", kAnimSelectorType, R"({ "rules": [
                { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Idle" },
                { "name": "walk", "priority": 10, "enter": [ { "fact": "Speed", "compare": "gt", "value": 0.1 } ],
                  "behavior": "Anim.Walk" } ] })");
            (void)Load("asset://bench/slots.sdata", kAnimSlotMapType, R"({ "rows": [
                { "behavior": "Anim.Idle", "clip": "asset://bench/idle.sanim" },
                { "behavior": "Anim.Walk", "clip": "asset://bench/walk.sanim" } ] })");
            Rig = Load("asset://bench/rig.sdata", kAnimRigType, R"({
                "skeleton": "asset://bench/crowd.sskel", "facts": "asset://bench/facts.sdata",
                "behaviors": [ "asset://bench/behaviors.sdata" ], "slot_maps": [ "asset://bench/slots.sdata" ],
                "layers": [ { "name": "anim.layer.base", "selector": "asset://bench/selector.sdata", "idle": "Anim.Idle" },
                            { "name": "anim.layer.upper", "selector": "asset://bench/selector.sdata", "idle": "Anim.Idle",
                              "mode": "additive", "weight": 0.5, "mask": [ { "joint": "j1" } ] } ] })");
        }
    };

    // Milliseconds per pose pass, averaged over ticks after a warm-up.
    double Measure(std::size_t characters, std::uint32_t workers)
    {
        Crowd crowd;
        for (std::size_t i = 0; i < characters; ++i)
            crowd.Characters.push_back(crowd.Character(crowd.Rig));
        std::unique_ptr<JobSystem> jobs = workers > 0 ? std::make_unique<JobSystem>(workers) : nullptr;
        AnimPoseSystem poser(jobs.get());
        double total = 0.0;
        int measured = 0;
        for (int tick = 0; tick < 240; ++tick)
        {
            for (std::size_t i = 0; i < crowd.Characters.size(); ++i)
                crowd.Motion(crowd.Characters[i]).Speed =
                    ((tick + static_cast<int>(i) * 7) / 40) % 2 == 0 ? 0.0f : 1.0f;
            crowd.Tick();
            const auto start = std::chrono::steady_clock::now();
            poser.Pose(crowd.Entities, crowd.Last(), AnimRigFixture::kTick);
            const auto end = std::chrono::steady_clock::now();
            if (tick >= 60)
            {
                total += std::chrono::duration<double, std::milli>(end - start).count();
                ++measured;
            }
        }
        return total / measured;
    }
}

TEST(AnimPoseBench, Measure)
{
    if (std::getenv("SENCHA_ANIM_POSE_BENCH") == nullptr)
        GTEST_SKIP() << "set SENCHA_ANIM_POSE_BENCH to measure the pose pass";
    for (const std::size_t characters : { std::size_t{ 1 }, std::size_t{ 64 }, std::size_t{ 256 } })
        for (const std::uint32_t workers : { 0u, 3u, 7u })
            std::printf("pose pass: %4zu characters, %u workers: %.3f ms/tick\n", characters, workers,
                        Measure(characters, workers));
}
