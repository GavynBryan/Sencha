#include <gtest/gtest.h>

#include <app/EngineSchedule.h>
#include <controller/LookIntegrationSystem.h>
#include <controller/LookOrientation.h>
#include <core/config/EngineConfig.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <input/InputActionResolve.h>
#include <input/InputActionState.h>
#include <input/InputFrame.h>
#include <movement/MovementIntent.h>
#include <net/ClientPrediction.h>
#include <net/NetTickEstimator.h>
#include <net/PawnCommandCapture.h>
#include <runtime/RuntimeFrameLoop.h>

#include <cstdint>
#include <vector>

//=============================================================================
// What a command record says the tick was aimed at.
//
// The authority applies each record's aim to the tick it names, so a record
// carrying an aim its tick was not simulated with is a pawn that turned
// somewhere the player did not -- and, on a catch-up frame, two ticks filed
// under one angle.
//=============================================================================

namespace
{
    struct CaptureHarness
    {
        // Without an intent the subject stands for something driven that does
        // not walk -- a fixed gun, which has an aim and a trigger and nothing
        // to move.
        explicit CaptureHarness(bool withMovementIntent = true)
        {
            WorldState.RegisterComponent<LookOrientation>();
            WorldState.RegisterComponent<LocalLookControl>();
            WorldState.RegisterComponent<MovementIntent>();
            Partitions.Add(StoragePartitionId::Default());

            Pawn = WorldState.CreateEntity();
            WorldState.AddComponent<LookOrientation>(Pawn, {});
            WorldState.AddComponent<LocalLookControl>(Pawn, {});
            if (withMovementIntent)
                WorldState.AddComponent<MovementIntent>(Pawn, {});

            WorldState.AddResource<InputActionState>().Configure(1);
            WorldState.AddResource<LookInputBinding>().Look = InputActionId{ 1 };

            Prediction.SetPredicted(Pawn);
            // A command needs a tick name both machines recognise; without an
            // estimate the capture has nothing to file the record under.
            Clock.Observe(100, 100, 0, 1.0 / 60.0);

            Schedule.Register<LookIntegrationSystem>();
            Schedule.Register<PawnCommandCaptureSystem>(Prediction, Clock);
            Schedule.After<PawnCommandCaptureSystem, LookIntegrationSystem>();
            Schedule.Init();
        }

        // One rendered frame running the tick count the accumulator asked for,
        // with the real latches doing the splitting the mapper does.
        void RunFrameWithTicks(std::uint32_t ticks, float lookX)
        {
            InputActionState& state = WorldState.GetResource<InputActionState>();

            Presentation.MotionX += lookX;
            Simulation.MotionX += lookX;

            const InputMotionDelta framePass = Presentation.Share(1);
            Presentation.Consume(framePass);
            state.FrameStorage()[0] =
                InputActionValue{ framePass.X, 0.0f, InputActionFlags::None };

            PreSimulateContext preSimulate{
                .Config = Config,
                .Runtime = Runtime,
                .Input = Input,
                .Entities = WorldState,
                .Partitions = Partitions,
            };
            Schedule.RunPreSimulate(preSimulate);

            for (std::uint32_t i = 0; i < ticks; ++i)
            {
                const std::uint32_t left = ticks - i;
                const InputMotionDelta tickPass = Simulation.Share(left);
                Simulation.Consume(tickPass);
                state.BeginTick(TickIndex)[0] =
                    InputActionValue{ tickPass.X, 0.0f, InputActionFlags::None };

                FixedLogicContext fixed{
                    .Config = Config,
                    .Runtime = Runtime,
                    .Time = FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = TickIndex },
                    .Entities = WorldState,
                    .Partitions = Partitions,
                    .TicksLeftInFrame = left,
                };
                Schedule.RunFixedLogic(fixed);
                ++TickIndex;
            }
        }

        // Oldest first, which is the order the ticks ran in.
        [[nodiscard]] std::vector<PawnCommandTick> Captured() const
        {
            std::vector<PawnCommandTick> records;
            const PawnCommandRing& ring = Prediction.Commands();
            for (std::size_t i = ring.Size(); i > 0; --i)
                records.push_back(ring.Recent(i - 1));
            return records;
        }

        EngineConfig Config;
        RuntimeFrameLoop Runtime;
        InputFrame Input;
        World WorldState;
        StoragePartitionSet Partitions;
        EngineSchedule Schedule;
        ClientPrediction Prediction;
        NetTickEstimator Clock;
        EntityId Pawn;

        InputEdgeLatch Presentation;
        InputEdgeLatch Simulation;
        std::uint64_t TickIndex = 1;
    };
}

// The defect this guards: the aim was sampled once per rendered frame, so every
// record a catch-up frame produced carried the same angle. The authority then
// applied that one angle to each of those ticks in turn, simulating them under
// an aim the client's own ticks never used.
TEST(PawnCommandCapture, EachTickIsFiledUnderTheAimItSimulatedWith)
{
    CaptureHarness harness;

    // A frame that ran no tick leaves its travel outstanding; the next frame
    // runs two and each takes half.
    harness.RunFrameWithTicks(0, 0.02f);
    harness.RunFrameWithTicks(2, 0.02f);

    const std::vector<PawnCommandTick> records = harness.Captured();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_NE(records[0].Yaw, records[1].Yaw)
        << "two ticks of one frame must not be filed under one aim";
    EXPECT_NEAR(records[0].Yaw, -0.02f, 1e-5f);
    EXPECT_NEAR(records[1].Yaw, -0.04f, 1e-5f);
}

// Not every driven thing walks, and the ones that do not still have to be
// heard: their actions are what the authority derives from and their aim is
// what it turns them by. Requiring an intent to move silenced them entirely --
// a player at a fixed gun sent nothing at all, so the authority never learned
// where they were pointing it and nobody watching saw it move.
TEST(PawnCommandCapture, ASubjectThatCannotWalkStillSendsItsAim)
{
    CaptureHarness harness(false);
    harness.RunFrameWithTicks(1, 0.02f);

    const std::vector<PawnCommandTick> records = harness.Captured();
    ASSERT_EQ(records.size(), 1u)
        << "a driven subject with no intent to move was never heard from";
    EXPECT_NEAR(records[0].Yaw, -0.02f, 1e-5f);
    EXPECT_EQ(records[0].Intent.WishDir, Vec3d::Zero())
        << "an absent intent must read as no intent to move";
    EXPECT_FALSE(records[0].Intent.Jump);
}

// The records name distinct ticks, so the authority can place each aim on the
// tick it belongs to.
TEST(PawnCommandCapture, ACatchUpFrameFilesOneRecordPerTick)
{
    CaptureHarness harness;
    harness.RunFrameWithTicks(3, 0.03f);

    const std::vector<PawnCommandTick> records = harness.Captured();
    ASSERT_EQ(records.size(), 3u);
    EXPECT_LT(records[0].Tick, records[1].Tick);
    EXPECT_LT(records[1].Tick, records[2].Tick);
}
