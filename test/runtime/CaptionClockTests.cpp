#include <gtest/gtest.h>

#include <app/GameContexts.h>
#include <audio/CaptionRuntime.h>
#include <audio/CaptionSystem.h>
#include <core/config/CaptionConfig.h>
#include <core/config/EngineConfig.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <input/InputFrame.h>
#include <runtime/RuntimeFrameLoop.h>

// Captions are read by a person, so they age on the wall clock. The audio phase
// runs once per rendered frame, so charging it a fixed tick's worth each time
// tied subtitle dwell to frame rate.

namespace
{
    EngineCaptionConfig OneChannel()
    {
        EngineCaptionConfig config;
        config.Channels.push_back({ .Name = "World", .MaxVisibleLines = 2 });
        return config;
    }

    CaptionPayload Line()
    {
        CaptionPayload payload;
        payload.Kind = CaptionKind::Subtitle;
        payload.Channel = "World";
        payload.Priority = CaptionPriority::Gameplay;
        payload.Text = "line.timed";
        return payload;
    }

    // Runs CaptionSystem through the real phase context, which is where the
    // clock choice lives; calling Update directly would bypass it.
    void RunAudioPhase(CaptionSystem& system,
                       World& world,
                       double wallDeltaSeconds,
                       double presentationDeltaSeconds)
    {
        EngineConfig config;
        RuntimeFrameLoop runtime;
        InputFrame input;
        StoragePartitionSet partitions;
        partitions.Add(StoragePartitionId::Default());

        AudioContext ctx{
            .Config = config,
            .Runtime = runtime,
            .Input = input,
            .WallDeltaSeconds = wallDeltaSeconds,
            .Presentation = PresentationTime{ .DeltaSeconds = presentationDeltaSeconds },
            .Entities = world,
            .Partitions = partitions,
        };
        system.Audio(ctx);
    }
}

TEST(CaptionClock, AgesByWallTimeNotByTickDelta)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, OneChannel());
    World world;

    CaptionSystem system(&captions);

    const CaptionId id = captions.BeginTimedCaption(Line(), 1.0f);
    ASSERT_TRUE(captions.IsActive(id));

    // One long frame. Presentation delta still reports a 60 Hz tick, so a
    // caption that consumed that instead would barely age at all.
    RunAudioPhase(system, world, 0.6, 1.0 / 60.0);
    EXPECT_TRUE(captions.IsActive(id));

    RunAudioPhase(system, world, 0.6, 1.0 / 60.0);
    EXPECT_FALSE(captions.IsActive(id));
}

TEST(CaptionClock, DoesNotAgeOnAFrameThatPassedNoWallTime)
{
    LoggingProvider logging;
    CaptionRuntime captions(logging, OneChannel());
    World world;

    CaptionSystem system(&captions);

    const CaptionId id = captions.BeginTimedCaption(Line(), 0.1f);
    ASSERT_TRUE(captions.IsActive(id));

    // Frames that advance no wall time must not retire a caption, however many
    // of them the renderer produces.
    for (int frame = 0; frame < 100; ++frame)
        RunAudioPhase(system, world, 0.0, 1.0 / 60.0);

    EXPECT_TRUE(captions.IsActive(id));
}
