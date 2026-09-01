// Clip playback advances on fixed ticks, which is what makes a pose the same
// on every machine regardless of how many ticks a frame happened to run.

#include <gtest/gtest.h>
#include <anim/AnimationComponentSchemas.h>

#include <anim/AnimationClipPlaybackSystem.h>
#include <anim/AnimationClipPlayerComponent.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>

namespace
{
    struct Fixture
    {
        AnimationClipCache Clips;
        World Entities;
        AnimationClipHandle Clip;

        Fixture()
        {
            AnimationClipData clip;
            clip.SkeletonPath = "asset://test.sskel";
            clip.DurationSeconds = 1.0f;
            AnimationJointTrack track;
            track.JointIndex = 0;
            track.Path = AnimationChannelPath::Translation;
            track.TimesSeconds = { 0.0f, 1.0f };
            track.Values = { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
            clip.Tracks.push_back(track);
            Clip = Clips.Register("asset://test.sanim", std::move(clip), {});

            Entities.RegisterComponent<AnimationClipPlayerComponent>();
        }

        EntityId AddPlayer(float rate, bool loop, float time = 0.0f)
        {
            const EntityId entity = Entities.CreateEntity();
            AnimationClipPlayerComponent player;
            player.Clip = Clip;
            player.Rate = rate;
            player.Loop = loop;
            player.TimeSeconds = time;
            Entities.AddComponent(entity, player);
            return entity;
        }

        void Tick(float dt, int count = 1)
        {
            StoragePartitionSet partitions;
            partitions.Add(StoragePartitionId::Default());
            for (int i = 0; i < count; ++i)
                AdvanceAnimationClipPlayers(Entities, partitions, Clips, dt);
        }

        [[nodiscard]] float TimeOf(EntityId entity) const
        {
            return Entities.TryGet<AnimationClipPlayerComponent>(entity)->TimeSeconds;
        }
    };
}

TEST(AnimationClipPlayback, TimeAdvancesByTheTickAndScalesWithRate)
{
    Fixture fixture;
    const EntityId normal = fixture.AddPlayer(1.0f, true);
    const EntityId double_ = fixture.AddPlayer(2.0f, true);
    const EntityId paused = fixture.AddPlayer(0.0f, true, 0.25f);

    fixture.Tick(0.1f, 3);

    EXPECT_NEAR(fixture.TimeOf(normal), 0.3f, 1e-5f);
    EXPECT_NEAR(fixture.TimeOf(double_), 0.6f, 1e-5f);
    // A paused player is an authored pose; nothing may move it.
    EXPECT_FLOAT_EQ(fixture.TimeOf(paused), 0.25f);
}

TEST(AnimationClipPlayback, LoopingWrapsAndHoldingClamps)
{
    Fixture fixture;
    const EntityId looping = fixture.AddPlayer(1.0f, true, 0.9f);
    const EntityId holding = fixture.AddPlayer(1.0f, false, 0.9f);
    const EntityId reversing = fixture.AddPlayer(-1.0f, true, 0.1f);

    fixture.Tick(0.2f);

    EXPECT_NEAR(fixture.TimeOf(looping), 0.1f, 1e-5f);
    EXPECT_NEAR(fixture.TimeOf(holding), 1.0f, 1e-5f);
    // Reverse playback wraps to the end rather than running negative.
    EXPECT_NEAR(fixture.TimeOf(reversing), 0.9f, 1e-5f);

    // A long stall advances by one duration at most, not by a walk.
    fixture.Tick(10.0f);
    EXPECT_GE(fixture.TimeOf(looping), 0.0f);
    EXPECT_LT(fixture.TimeOf(looping), 1.0f);
}

TEST(AnimationClipPlayback, TheSameTickSequenceProducesTheSameTime)
{
    // Determinism under catch-up: 6 ticks in one frame and 2 ticks across
    // three frames are the same six advances, so the pose is identical.
    Fixture burst;
    const EntityId a = burst.AddPlayer(1.0f, true);
    burst.Tick(1.0f / 60.0f, 6);

    Fixture spread;
    const EntityId b = spread.AddPlayer(1.0f, true);
    for (int frame = 0; frame < 3; ++frame)
        spread.Tick(1.0f / 60.0f, 2);

    EXPECT_FLOAT_EQ(burst.TimeOf(a), spread.TimeOf(b));
}
