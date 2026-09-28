// Measures the pose pass on a crowd of 62-joint, two-layer characters, posed
// serially and across workers. Env-gated; run it from a Release build:
//   SENCHA_ANIM_POSE_BENCH=1 build-profile/test/runtime_tests --gtest_filter='AnimPoseBench.*'

#include "AnimCrowdFixture.h"

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
    // Milliseconds per pose pass, averaged over ticks after a warm-up.
    double Measure(std::size_t characters, std::uint32_t workers)
    {
        AnimCrowd::Fixture crowd;
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
