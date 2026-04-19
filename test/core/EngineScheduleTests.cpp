#include <gtest/gtest.h>
#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <core/config/EngineConfig.h>
#include <input/InputFrame.h>
#include <runtime/RuntimeFrameLoop.h>
#include <zone/ZoneRuntime.h>

namespace
{
    static std::vector<std::string> CallLog;

    struct ScheduleHarness
    {
        EngineConfig Config;
        Engine EngineInstance{ Config };
        RuntimeFrameLoop Runtime;
        InputFrame Input;
        ZoneRuntime Zones;
        EngineSchedule Schedule;

        FrameRegistryView BuildView()
        {
            return Schedule.BuildFrameView(Zones);
        }
    };

    struct FixedA
    {
        void Init() { CallLog.push_back("A::Init"); }
        void FixedLogic(FixedLogicContext&) { CallLog.push_back("A::FixedLogic"); }
        void Shutdown() { CallLog.push_back("A::Shutdown"); }
    };

    struct FixedB
    {
        void Init() { CallLog.push_back("B::Init"); }
        void FixedLogic(FixedLogicContext&) { CallLog.push_back("B::FixedLogic"); }
        void Shutdown() { CallLog.push_back("B::Shutdown"); }
    };

    struct FixedC
    {
        void FixedLogic(FixedLogicContext&) { CallLog.push_back("C::FixedLogic"); }
    };

    struct MultiPhase
    {
        int FixedCount = 0;
        int FrameCount = 0;
        int ExtractCount = 0;

        void FixedLogic(FixedLogicContext&) { ++FixedCount; }
        void FrameUpdate(FrameUpdateContext&) { ++FrameCount; }
        void ExtractRender(RenderExtractContext&) { ++ExtractCount; }
    };

    class EngineScheduleTest : public ::testing::Test
    {
    protected:
        void SetUp() override { CallLog.clear(); }
    };
}

TEST_F(EngineScheduleTest, RegisterAndGet)
{
    EngineSchedule schedule;
    schedule.Register<FixedA>();

    EXPECT_NE(schedule.Get<FixedA>(), nullptr);
    EXPECT_TRUE(schedule.Has<FixedA>());
    EXPECT_FALSE(schedule.Has<FixedB>());
}

TEST_F(EngineScheduleTest, InitAndShutdownUseRegistrationAndReverseOrder)
{
    EngineSchedule schedule;
    schedule.Register<FixedA>();
    schedule.Register<FixedB>();

    schedule.Init();
    EXPECT_EQ(CallLog, (std::vector<std::string>{ "A::Init", "B::Init" }));

    CallLog.clear();
    schedule.Shutdown();
    EXPECT_EQ(CallLog, (std::vector<std::string>{ "B::Shutdown", "A::Shutdown" }));
}

TEST_F(EngineScheduleTest, DependencyOrderingAppliesWithinPhase)
{
    ScheduleHarness harness;
    harness.Zones.CreateZone(ZoneId{ 1 });
    harness.Zones.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Logic = true });
    harness.Schedule.Register<FixedC>();
    harness.Schedule.Register<FixedB>();
    harness.Schedule.Register<FixedA>();
    harness.Schedule.After<FixedB, FixedA>();
    harness.Schedule.After<FixedC, FixedB>();
    harness.Schedule.Init();

    FrameRegistryView view = harness.BuildView();
    FixedLogicContext ctx{
        .EngineInstance = harness.EngineInstance,
        .Config = harness.Config,
        .Runtime = harness.Runtime,
        .Input = harness.Input,
        .Time = {},
        .Registries = view,
        .ActiveRegistries = view.Logic,
    };
    harness.Schedule.RunFixedLogic(ctx);

    EXPECT_EQ(CallLog, (std::vector<std::string>{
        "B::Init",
        "A::Init",
        "A::FixedLogic",
        "B::FixedLogic",
        "C::FixedLogic",
    }));
}

TEST_F(EngineScheduleTest, DispatchesOnlyImplementedPhases)
{
    ScheduleHarness harness;
    harness.Zones.CreateZone(ZoneId{ 1 });
    harness.Zones.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Visible = true, .Logic = true });
    auto& system = harness.Schedule.Register<MultiPhase>();
    harness.Schedule.Init();

    FrameRegistryView view = harness.BuildView();
    FixedLogicContext fixed{
        .EngineInstance = harness.EngineInstance,
        .Config = harness.Config,
        .Runtime = harness.Runtime,
        .Input = harness.Input,
        .Time = {},
        .Registries = view,
        .ActiveRegistries = view.Logic,
    };
    harness.Schedule.RunFixedLogic(fixed);

    FrameUpdateContext frame{
        .EngineInstance = harness.EngineInstance,
        .Config = harness.Config,
        .Runtime = harness.Runtime,
        .Input = harness.Input,
        .WallDeltaSeconds = 0.016,
        .Presentation = {},
        .Registries = view,
        .ActiveRegistries = view.Logic,
    };
    harness.Schedule.RunFrameUpdate(frame);

    RenderPacketDoubleBuffer packets;
    RenderExtractContext extract{
        .EngineInstance = harness.EngineInstance,
        .Config = harness.Config,
        .Runtime = harness.Runtime,
        .Input = harness.Input,
        .PacketWrite = packets.WriteSlot(),
        .PacketRead = packets.ReadSlot(),
        .Presentation = {},
        .Registries = view,
        .ActiveRegistries = view.Visible,
    };
    harness.Schedule.RunExtractRender(extract);

    EXPECT_EQ(system.FixedCount, 1);
    EXPECT_EQ(system.FrameCount, 1);
    EXPECT_EQ(system.ExtractCount, 1);
}

TEST_F(EngineScheduleTest, BuildFrameViewUsesZoneParticipation)
{
    ScheduleHarness harness;
    harness.Zones.CreateZone(ZoneId{ 1 });
    harness.Zones.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Logic = true });
    harness.Zones.CreateZone(ZoneId{ 2 });
    harness.Zones.SetParticipation(ZoneId{ 2 }, ZoneParticipation{ .Visible = true, .Physics = true });

    FrameRegistryView view = harness.BuildView();

    ASSERT_NE(view.Global, nullptr);
    EXPECT_EQ(view.Logic.size(), 1u);
    EXPECT_EQ(view.Physics.size(), 1u);
    EXPECT_EQ(view.Visible.size(), 1u);
    EXPECT_EQ(view.Logic[0]->Zone, ZoneId{ 1 });
    EXPECT_EQ(view.Physics[0]->Zone, ZoneId{ 2 });
    EXPECT_EQ(view.Visible[0]->Zone, ZoneId{ 2 });
}
