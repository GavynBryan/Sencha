#include <gtest/gtest.h>

#include <app/EngineSchedule.h>
#include <core/config/EngineConfig.h>
#include <ecs/WorldComponentSchema.h>
#include <input/InputFrame.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeWorld.h>

namespace
{
std::vector<std::string> CallLog;

WorldComponentSchema EmptySchema()
{
    WorldComponentSchema schema;
    schema.Seal();
    return schema;
}

struct ScheduleHarness
{
    ScheduleHarness()
        : Schema(EmptySchema())
        , RuntimeWorldState(Schema)
    {
    }

    const FrameZoneView& BuildView()
    {
        (void)RuntimeWorldState.BeginResidencyProcessing();
        RuntimeWorldState.FinalizeResidencyProcessing();
        return RuntimeWorldState.BuildFrameView();
    }

    void EndView()
    {
        RuntimeWorldState.EndFrameView();
    }

    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    InputFrame Input;
    WorldComponentSchema Schema;
    RuntimeWorld RuntimeWorldState;
    EngineSchedule Schedule;
};

struct FixedA
{
    void Init() { CallLog.push_back("A::Init"); }
    void FixedLogic(FixedLogicContext&)
    {
        CallLog.push_back("A::FixedLogic");
    }
    void Shutdown() { CallLog.push_back("A::Shutdown"); }
};

struct FixedB
{
    void Init() { CallLog.push_back("B::Init"); }
    void FixedLogic(FixedLogicContext&)
    {
        CallLog.push_back("B::FixedLogic");
    }
    void Shutdown() { CallLog.push_back("B::Shutdown"); }
};

struct FixedC
{
    void FixedLogic(FixedLogicContext&)
    {
        CallLog.push_back("C::FixedLogic");
    }
};

struct MultiPhase
{
    int PreSimulateCount = 0;
    int FixedCount = 0;
    int FrameCount = 0;
    int ExtractCount = 0;

    void PreSimulate(PreSimulateContext&) { ++PreSimulateCount; }
    void FixedLogic(FixedLogicContext&) { ++FixedCount; }
    void FrameUpdate(FrameUpdateContext&) { ++FrameCount; }
    void ExtractRender(RenderExtractContext&) { ++ExtractCount; }
};

class EngineScheduleTest : public ::testing::Test
{
protected:
    void SetUp() override { CallLog.clear(); }
};
} // namespace

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
    EXPECT_EQ(
        CallLog,
        (std::vector<std::string>{ "A::Init", "B::Init" }));

    CallLog.clear();
    schedule.Shutdown();
    EXPECT_EQ(
        CallLog,
        (std::vector<std::string>{
            "B::Shutdown",
            "A::Shutdown",
        }));
}

TEST_F(EngineScheduleTest, DependencyOrderingAppliesWithinPhase)
{
    ScheduleHarness harness;
    harness.RuntimeWorldState.AttachZone(
        ZoneId{ 1 },
        ZoneParticipation{ .Logic = true });
    harness.Schedule.Register<FixedC>();
    harness.Schedule.Register<FixedB>();
    harness.Schedule.Register<FixedA>();
    harness.Schedule.After<FixedB, FixedA>();
    harness.Schedule.After<FixedC, FixedB>();
    harness.Schedule.Init();

    const FrameZoneView& view = harness.BuildView();
    FixedLogicContext context{
        .Config = harness.Config,
        .Runtime = harness.Runtime,
        .Input = harness.Input,
        .Time = {},
        .Entities = *view.Entities,
        .Partitions = view.Logic,
    };
    harness.Schedule.RunFixedLogic(context);
    harness.EndView();

    EXPECT_EQ(
        CallLog,
        (std::vector<std::string>{
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
    harness.RuntimeWorldState.AttachZone(
        ZoneId{ 1 },
        ZoneParticipation{ .Visible = true, .Logic = true });
    MultiPhase& system = harness.Schedule.Register<MultiPhase>();
    harness.Schedule.Init();

    const FrameZoneView& view = harness.BuildView();
    PreSimulateContext preSimulate{
        .Config = harness.Config,
        .Runtime = harness.Runtime,
        .Input = harness.Input,
        .Entities = *view.Entities,
        .Partitions = view.Logic,
    };
    harness.Schedule.RunPreSimulate(preSimulate);

    FixedLogicContext fixed{
        .Config = harness.Config,
        .Runtime = harness.Runtime,
        .Input = harness.Input,
        .Time = {},
        .Entities = *view.Entities,
        .Partitions = view.Logic,
    };
    harness.Schedule.RunFixedLogic(fixed);

    FrameUpdateContext frame{
        .Config = harness.Config,
        .Runtime = harness.Runtime,
        .Input = harness.Input,
        .WallDeltaSeconds = 0.016,
        .Presentation = {},
        .Entities = *view.Entities,
        .Partitions = view.Logic,
    };
    harness.Schedule.RunFrameUpdate(frame);

    RenderPacketDoubleBuffer packets;
    RenderExtractContext extract{
        .Config = harness.Config,
        .Runtime = harness.Runtime,
        .Input = harness.Input,
        .PacketWrite = packets.WriteSlot(),
        .PacketRead = packets.ReadSlot(),
        .Presentation = {},
        .Entities = *view.Entities,
        .Partitions = view.Visible,
    };
    harness.Schedule.RunExtractRender(extract);
    harness.EndView();

    EXPECT_EQ(system.PreSimulateCount, 1);
    EXPECT_EQ(system.FixedCount, 1);
    EXPECT_EQ(system.FrameCount, 1);
    EXPECT_EQ(system.ExtractCount, 1);
}

TEST_F(EngineScheduleTest, RuntimeWorldBuildsDomainPartitionView)
{
    ScheduleHarness harness;
    RuntimeZoneRecord& logic = harness.RuntimeWorldState.AttachZone(
        ZoneId{ 1 },
        ZoneParticipation{ .Logic = true });
    RuntimeZoneRecord& physics = harness.RuntimeWorldState.AttachZone(
        ZoneId{ 2 },
        ZoneParticipation{ .Visible = true, .Physics = true });

    const FrameZoneView& view = harness.BuildView();

    EXPECT_EQ(view.Entities, &harness.RuntimeWorldState.Entities());
    EXPECT_TRUE(view.Logic.Contains(PersistentStoragePartition));
    EXPECT_TRUE(view.Physics.Contains(PersistentStoragePartition));
    EXPECT_TRUE(view.Visible.Contains(PersistentStoragePartition));
    EXPECT_TRUE(view.Logic.Contains(logic.Partition));
    EXPECT_FALSE(view.Visible.Contains(logic.Partition));
    EXPECT_TRUE(view.Physics.Contains(physics.Partition));
    EXPECT_TRUE(view.Visible.Contains(physics.Partition));
    EXPECT_FALSE(view.Logic.Contains(physics.Partition));

    harness.EndView();
}
