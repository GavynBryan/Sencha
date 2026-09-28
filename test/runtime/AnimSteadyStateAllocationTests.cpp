// Once every state has been reached, a tick of the animation pipeline -- facts,
// selection, content, events and poses -- allocates nothing, whether the pose
// pass runs serially or across workers.

#include "AllocationCounter.h"
#include "AnimCrowdFixture.h"

#include <anim/AnimEventSystem.h>
#include <anim/AnimPoseSystem.h>
#include <anim/AnimRequests.h>
#include <jobs/JobSystem.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{
    struct Pipeline : AnimCrowd::Fixture
    {
        DataAssetHandle CharacterRig;
        std::vector<EntityId> Requesters;
        std::vector<AnimRequestId> Held;
        GameplayTagId Reload;
        AnimEventSystem Events{ nullptr, true };
        std::unique_ptr<JobSystem> Jobs;
        std::unique_ptr<AnimPoseSystem> Poses;

        explicit Pipeline(std::uint32_t workers)
        {
            AnimCharacterRig::RegisterTags(*this);
            CharacterRig = AnimCharacterRig::Load(*this);
            for (int i = 0; i < 8; ++i)
                Characters.push_back(Character(Rig));
            for (int i = 0; i < 4; ++i)
                Requesters.push_back(Character(CharacterRig));
            Held.resize(Requesters.size());
            Reload = Tag("anim.intent.reload");
            Jobs = workers > 0 ? std::make_unique<JobSystem>(workers) : nullptr;
            Poses = std::make_unique<AnimPoseSystem>(Jobs.get());
        }

        // Input varies with the tick so every transition keeps recurring: speed
        // changes, landings, crouching, and requests issued and cancelled.
        void Step(int tick)
        {
            for (std::size_t i = 0; i < Characters.size(); ++i)
                Motion(Characters[i]).Speed = ((tick + static_cast<int>(i) * 7) / 40) % 2 == 0 ? 0.0f : 1.0f;
            for (std::size_t i = 0; i < Requesters.size(); ++i)
            {
                const int phase = tick + static_cast<int>(i) * 11;
                AnimTestMotion& motion = Motion(Requesters[i]);
                motion.Speed = static_cast<float>((phase / 30) % 3) * 1.25f;
                motion.Grounded = (phase / 50) % 2 == 0;
                motion.Crouched = (phase / 70) % 2 == 0;
                if (phase % 90 == 0)
                {
                    AnimRequestDesc desc;
                    desc.Source = Requesters[i];
                    desc.Intent = Reload;
                    Held[i] = IssueAnimRequest(Entities, Requesters[i], desc, Now).Id;
                }
                else if (phase % 90 == 45)
                    (void)CancelAnimRequest(Entities, Requesters[i], Held[i], AnimCancelReason::Released, Now);
            }
            Tick();
            Events.Run(Entities, Last(), kTick);
            Poses->Pose(Entities, Last(), kTick);
        }
    };

    void ExpectNoSteadyStateAllocation(std::uint32_t workers)
    {
        Pipeline pipeline(workers);
        int tick = 0;
        for (; tick < 600; ++tick)
            pipeline.Step(tick);
        const std::size_t before = AllocationCount();
        for (; tick < 1200; ++tick)
            pipeline.Step(tick);
        EXPECT_EQ(AllocationCount() - before, 0u) << "allocations in 600 steady ticks with " << workers << " workers";
    }
}

TEST(AnimSteadyStateAllocation, SerialTicksAllocateNothing)
{
    ExpectNoSteadyStateAllocation(0);
}

TEST(AnimSteadyStateAllocation, TicksAcrossWorkersAllocateNothing)
{
    ExpectNoSteadyStateAllocation(3);
}
