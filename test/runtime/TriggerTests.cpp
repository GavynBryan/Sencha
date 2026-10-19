#include "AllocationCounter.h"
#include "BenchRecorder.h"
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <assets/data/DataAssetCache.h>
#include <authored/WorldVocabulary.h>
#include <core/assets/AssetRegistry.h>
#include <core/console/ConsoleRegistry.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <gtest/gtest.h>
#include <logic/TriggerSystem.h>
#include <world/SimulationAuthority.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/transform/TransformComponents.h>

struct TriggerEligible
{
};
SENCHA_DECLARE_COMPONENT_TYPE(TriggerEligible, "test.trigger_eligible");

namespace
{
JsonValue Parse(std::string_view text)
{
    auto result = JsonParse(text);
    EXPECT_TRUE(result.has_value());
    return result.value_or(EmptyTriggerProgram());
}
JsonValue Program(int first = 1, int second = 2)
{
    auto source = EmptyTriggerProgram();
    for (int number : {first, second})
        source.Find("commands")
            ->AsArray()
            .push_back(JsonValue::Object{
                {"verb", "test.record"},
                {"arguments", JsonValue::Object{{"target", JsonValue::Object{{"input", "activator"}}},
                                                {"number", JsonValue::Object{{"const", number}}},
                                                {"direction", JsonValue::Object{{"input", "trigger_up"}}}}}});
    return source;
}
struct Recorder
{
    struct Call
    {
        EntityId Target, Producer, Instigator;
        std::int64_t Number = 0;
        std::uint64_t Tick = 0;
        AuthoredVectorValue Direction;
    };
    std::vector<Call> Calls;
    World *Entities = nullptr;
    bool DestroyProducer = false;
    bool Capture = true;
    std::uint64_t Total = 0;
    VerbAdmission Result = VerbAdmission::Accepted;
    VerbAdmission Invoke(const VerbInvocation &invocation)
    {
        Call call;
        call.Producer = invocation.Producer;
        call.Instigator = invocation.Instigator;
        call.Tick = invocation.Tick;
        (void)invocation.Arguments->TryGetEntity(0, call.Target);
        (void)invocation.Arguments->TryGetInt(1, call.Number);
        (void)invocation.Arguments->TryGetVector(2, call.Direction);
        ++Total;
        if (Capture)
            Calls.push_back(call);
        if (DestroyProducer)
            Entities->DestroyEntity(invocation.Producer);
        return Result;
    }
};
struct Gate
{
    bool Allow = true;
    mutable std::size_t Calls = 0;
    mutable EntityId Target;
    static AuthoredQueryStatus Evaluate(const Gate &gate, std::span<const AuthoredValue> arguments,
                                        AuthoredValue &result)
    {
        ++gate.Calls;
        (void)arguments[0].TryGetEntity(gate.Target);
        result = AuthoredValue::Bool(gate.Allow);
        return AuthoredQueryStatus::Value;
    }
};
class TriggerFixture : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        Entities.RegisterComponent<Trigger>();
        Entities.RegisterComponent<TriggerEligible>();
        Entities.RegisterComponent<WorldTransform>();
        InstallAuthoredVocabulary(Entities);
        Verbs = FindVerbRegistry(Entities);
        QueryCatalog = FindAuthoredQueryRegistry(Entities);
        Declare("test.record");
        AuthoredQueryDefinition condition;
        condition.Name = "test.allowed";
        condition.Arguments.Children.push_back(MakeDataField(DataFieldKind::Entity, "target", "Target"));
        condition.Result = MakeDataField(DataFieldKind::Bool, "result", "Allowed");
        AuthoredQueryRegistrationScope scope(*QueryCatalog, "test");
        (void)scope.Declare(std::move(condition));
        ASSERT_TRUE(scope.Commit());
        Dispatcher = std::make_unique<VerbDispatcher>(*Verbs);
        Dispatcher->SetTrace(&Trace);
        QueryDispatcher = std::make_unique<AuthoredQueryDispatcher>(*QueryCatalog);
        Record.Entities = &Entities;
        Record.Calls.reserve(4096);
        Token = Dispatcher->Bind(Verbs->Find("test.record"), Record);
        GateToken = QueryDispatcher->Bind<&Gate::Evaluate>(QueryCatalog->Find("test.allowed"), Condition);
        System =
            &RegisterTriggerSystem(Schedule, Entities, *Dispatcher, *QueryDispatcher, Assets, Cache, Console, Logging);
        Logic.Add(StoragePartitionId::Default());
        Entities.AddResource<SensorOverlaps>();
        Sensor = Entities.CreateEntity();
        Entities.AddComponent<Trigger>(Sensor);
        Entities.AddComponent<WorldTransform>(Sensor);
        Activator = Entities.CreateEntity();
        Entities.AddComponent<TriggerEligible>(Activator);
        ASSERT_TRUE(SetTriggerProgram(Entities, Sensor, Program()));
        Schedule.Init();
    }
    void Declare(std::string name)
    {
        VerbDefinition definition;
        definition.Name = std::move(name);
        definition.Arguments.Children = {MakeDataField(DataFieldKind::Entity, "target", "Target"),
                                         MakeDataField(DataFieldKind::Int, "number", "Number"),
                                         MakeDataField(DataFieldKind::Vector, "direction", "Direction")};
        VerbRegistrationScope scope(*Verbs, "test");
        (void)scope.Declare(std::move(definition));
        ASSERT_TRUE(scope.Commit());
    }
    void Sample(std::uint64_t tick, std::initializer_list<EntityId> occupants)
    {
        auto &overlaps = Entities.GetResource<SensorOverlaps>();
        overlaps.Begin();
        for (auto activator : occupants)
            ASSERT_TRUE(overlaps.Observe({Sensor, activator}, true));
        ASSERT_TRUE(overlaps.Commit(tick));
    }
    void Tick(std::uint64_t tick, double dt = 0.1)
    {
        FixedLogicContext context{Config, Runtime, {dt, tick}, Entities, Logic};
        Schedule.RunFixedLogic(context);
    }
    Trigger &Settings()
    {
        return *Entities.TryGet<Trigger>(Sensor);
    }
    void AddCondition(JsonValue &source)
    {
        source.Find("conditions")
            ->AsArray()
            .push_back(Parse(R"({"query":"test.allowed","arguments":{"target":{"input":"activator"}}})"));
    }
    LoggingProvider Logging;
    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    ConsoleRegistry Console;
    AssetRegistry Assets{Logging};
    DataAssetCache Cache;
    World Entities;
    StoragePartitionSet Logic;
    EngineSchedule Schedule;
    VerbRegistry *Verbs = nullptr;
    AuthoredQueryRegistry *QueryCatalog = nullptr;
    std::unique_ptr<VerbDispatcher> Dispatcher;
    std::unique_ptr<AuthoredQueryDispatcher> QueryDispatcher;
    VerbBindingToken Token;
    AuthoredQueryBindingToken GateToken;
    VerbTraceRing Trace{4096};
    Recorder Record;
    Gate Condition;
    TriggerSystem *System = nullptr;
    EntityId Sensor, Activator;
};
} // namespace

TEST_F(TriggerFixture, CommandsUseDispatcherInOrderOnTheFollowingTick)
{
    Sample(1, {Activator});
    Tick(1);
    EXPECT_TRUE(Record.Calls.empty());
    Tick(2);
    ASSERT_EQ(Record.Calls.size(), 2u);
    EXPECT_EQ(Record.Calls[0].Number, 1);
    EXPECT_EQ(Record.Calls[1].Number, 2);
    EXPECT_EQ(Record.Calls[0].Target, Activator);
    EXPECT_EQ(Record.Calls[0].Producer, Sensor);
    EXPECT_EQ(Record.Calls[0].Instigator, Activator);
    EXPECT_EQ(Record.Calls[0].Tick, 2u);
    Tick(2);
    Tick(3);
    EXPECT_EQ(Record.Calls.size(), 2u);
}
TEST_F(TriggerFixture, DisabledUnknownCommandsSurviveAndDoNotExecute)
{
    auto source = Program();
    auto &row = source.Find("commands")->AsArray()[0];
    row.AsObject().emplace_back("enabled", false);
    *row.Find("verb") = JsonValue("missing.verb");
    ASSERT_TRUE(SetTriggerProgram(Entities, Sensor, source));
    Sample(1, {Activator});
    Tick(2);
    ASSERT_EQ(Record.Calls.size(), 1u);
    EXPECT_EQ(Record.Calls[0].Number, 2);
    EXPECT_EQ(TriggerProgramSource(Entities, Sensor)->Find("commands")->Size(), 2u);
}
TEST_F(TriggerFixture, QueryConditionsResolveActivatorAndShortCircuit)
{
    auto source = Program();
    AddCondition(source);
    AddCondition(source);
    ASSERT_TRUE(SetTriggerProgram(Entities, Sensor, source));
    Condition.Allow = false;
    Sample(1, {Activator});
    Tick(2);
    EXPECT_TRUE(Record.Calls.empty());
    EXPECT_EQ(Condition.Calls, 1u);
    EXPECT_EQ(Condition.Target, Activator);
    Sample(2, {});
    Tick(3);
    Condition.Allow = true;
    Sample(3, {Activator});
    Tick(4);
    EXPECT_EQ(Record.Calls.size(), 2u);
    EXPECT_EQ(Condition.Calls, 3u);
}
TEST_F(TriggerFixture, EligibilityUsesGenericComponentIdentities)
{
    auto source = Program();
    source.Find("require_components")->AsArray().emplace_back("test.trigger_eligible");
    ASSERT_TRUE(SetTriggerProgram(Entities, Sensor, source));
    const auto other = Entities.CreateEntity();
    Sample(1, {other, Activator});
    Tick(2);
    ASSERT_EQ(Record.Calls.size(), 2u);
    EXPECT_EQ(Record.Calls[0].Target, Activator);
}
TEST_F(TriggerFixture, StayIsRateLimitedAndNeverReplaysAnOldSample)
{
    Settings().Event = TriggerEvent::Stay;
    Settings().StayInterval = 0.25f;
    for (std::uint64_t tick = 1; tick <= 7; ++tick)
    {
        Sample(tick, {Activator});
        Tick(tick + 1);
    }
    EXPECT_EQ(Record.Calls.size(), 6u);
    Tick(9);
    EXPECT_EQ(Record.Calls.size(), 6u);
}
TEST_F(TriggerFixture, ExitExecutesOnlyOnDeparture)
{
    Settings().Event = TriggerEvent::Exit;
    Sample(1, {Activator});
    Tick(2);
    Sample(2, {Activator});
    Tick(3);
    EXPECT_TRUE(Record.Calls.empty());
    Sample(3, {});
    Tick(4);
    EXPECT_EQ(Record.Calls.size(), 2u);
}
TEST_F(TriggerFixture, CooldownDropsReentryRatherThanDelayingIt)
{
    Settings().Cooldown = 0.5f;
    Sample(1, {Activator});
    Tick(2);
    Sample(2, {});
    Tick(3);
    Sample(3, {Activator});
    Tick(4);
    EXPECT_EQ(Record.Calls.size(), 2u);
    Sample(4, {});
    Tick(5);
    Sample(5, {});
    Tick(6);
    Sample(6, {});
    Tick(7);
    Sample(7, {Activator});
    Tick(8);
    EXPECT_EQ(Record.Calls.size(), 4u);
}
TEST_F(TriggerFixture, CooldownScopeCanBePerActivatorOrGlobal)
{
    Settings().Cooldown = 2;
    const auto other = Entities.CreateEntity();
    Sample(1, {Activator, other});
    Tick(2);
    EXPECT_EQ(Record.Calls.size(), 4u);
    Settings().Enabled = false;
    Tick(3);
    Settings().Enabled = true;
    Settings().PerActivator = false;
    Sample(3, {Activator, other});
    Tick(4);
    EXPECT_EQ(Record.Calls.size(), 6u);
}
TEST_F(TriggerFixture, OncePersistsAcrossNormalDeparture)
{
    Settings().Once = true;
    Sample(1, {Activator});
    Tick(2);
    Sample(2, {});
    Tick(3);
    Sample(3, {Activator});
    Tick(4);
    EXPECT_EQ(Record.Calls.size(), 2u);
}
TEST_F(TriggerFixture, EnablingInsideAnOccupiedSensorStartsWithEnter)
{
    Settings().Enabled = false;
    Sample(1, {Activator});
    Tick(2);
    Settings().Enabled = true;
    Sample(2, {Activator});
    Tick(3);
    EXPECT_EQ(Record.Calls.size(), 2u);
}
TEST_F(TriggerFixture, ClientSamplesCannotExecuteOrReplayGameplay)
{
    Entities.AddResource<SimulationAuthority>().Authoritative = false;
    Sample(1, {Activator});
    Tick(2);
    EXPECT_TRUE(Record.Calls.empty());
    Entities.GetResource<SimulationAuthority>().Authoritative = true;
    Tick(3);
    EXPECT_TRUE(Record.Calls.empty());
    Sample(3, {Activator});
    Tick(4);
    EXPECT_EQ(Record.Calls.size(), 2u);
}
TEST_F(TriggerFixture, DestroyedActivatorCannotAliasAReusedEntityIndex)
{
    Sample(1, {Activator});
    const auto old = Activator;
    Entities.DestroyEntity(Activator);
    Activator = Entities.CreateEntity();
    EXPECT_NE(old, Activator);
    Tick(2);
    EXPECT_TRUE(Record.Calls.empty());
    Sample(2, {Activator});
    Tick(3);
    EXPECT_EQ(Record.Calls.size(), 2u);
}
TEST_F(TriggerFixture, DormantPartitionsDiscardPendingOccurrences)
{
    Sample(1, {Activator});
    Logic.Clear();
    Tick(2);
    Logic.Add(StoragePartitionId::Default());
    Tick(3);
    EXPECT_TRUE(Record.Calls.empty());
    Sample(3, {Activator});
    Tick(4);
    EXPECT_EQ(Record.Calls.size(), 2u);
}
TEST_F(TriggerFixture, ProducerDestructionDuringDispatchIsSafeAndStopsTheStack)
{
    Record.DestroyProducer = true;
    Sample(1, {Activator});
    Tick(2);
    EXPECT_EQ(Record.Calls.size(), 1u);
    EXPECT_EQ(TriggerProgramsOf(Entities).Size(), 0u);
}
TEST_F(TriggerFixture, RefusalStopsTheStackAndIsTraced)
{
    Record.Result = VerbAdmission::Refused;
    Sample(1, {Activator});
    Tick(2);
    EXPECT_EQ(Record.Calls.size(), 1u);
    EXPECT_EQ(System->Stats().Failed, 1u);
    EXPECT_FALSE(Trace.Snapshot().empty());
}
TEST_F(TriggerFixture, BudgetRefusesWholeActivations)
{
    System->SetLimits(32, 1);
    Sample(1, {Activator});
    Tick(2);
    EXPECT_TRUE(Record.Calls.empty());
    EXPECT_EQ(System->Stats().Overflowed, 1u);
    System->SetLimits(32, 2);
    Sample(2, {});
    Tick(3);
    Sample(3, {Activator});
    Tick(4);
    EXPECT_EQ(Record.Calls.size(), 2u);
}
TEST_F(TriggerFixture, SweptTransientEntryDoesNotLeavePermanentOccupancy)
{
    auto &overlaps = Entities.GetResource<SensorOverlaps>();
    overlaps.Begin();
    ASSERT_TRUE(overlaps.Observe({Sensor, Activator}, false));
    ASSERT_TRUE(overlaps.Commit(1));
    Tick(2);
    EXPECT_EQ(Record.Calls.size(), 2u);
    Tick(3);
    EXPECT_EQ(System->RetainedPairs(), 0u);
}
TEST_F(TriggerFixture, TriggerAxesRotateDirectionBindings)
{
    Entities.TryGet<WorldTransform>(Sensor)->Value.Rotation = Quatf::FromAxisAngle(Vec3d(0, 0, 1), 1.57079632679f);
    Sample(1, {Activator});
    Tick(2);
    ASSERT_EQ(Record.Calls.size(), 2u);
    EXPECT_NEAR(Record.Calls[0].Direction.Components[0], -1, 0.0001);
    EXPECT_NEAR(Record.Calls[0].Direction.Components[1], 0, 0.0001);
}
TEST_F(TriggerFixture, ProgramCopiesOwnIndependentSourceAndOnlyEditedProgramsRecompile)
{
    Sample(1, {Activator});
    Tick(2);
    auto &programs = TriggerProgramsOf(Entities);
    EXPECT_EQ(programs.RebuildCount(), 1u);
    const auto copy = Entities.CreateEntity();
    Entities.AddComponent<Trigger>(copy, Settings());
    ASSERT_TRUE(SetTriggerProgram(Entities, copy, Program(8, 9)));
    EXPECT_NE(Entities.TryGet<Trigger>(copy)->Program, Settings().Program);
    Sample(2, {});
    Tick(3);
    Sample(3, {Activator});
    Tick(4);
    EXPECT_EQ(programs.RebuildCount(), 2u);
    ASSERT_TRUE(SetTriggerProgram(Entities, Sensor, Program(4, 5)));
    Sample(4, {});
    Tick(5);
    Sample(5, {Activator});
    Tick(6);
    EXPECT_EQ(programs.RebuildCount(), 3u);
    EXPECT_EQ(Record.Calls.back().Number, 5);
    Entities.DestroyEntity(copy);
    EXPECT_EQ(programs.Size(), 1u);
}
TEST_F(TriggerFixture, UnresolvedActiveCommandsRefuseTheWholeProgramAndRetryOnCatalogChange)
{
    auto source = Program();
    *source.Find("commands")->AsArray()[1].Find("verb") = JsonValue("test.late");
    ASSERT_TRUE(SetTriggerProgram(Entities, Sensor, source));
    Sample(1, {Activator});
    Tick(2);
    EXPECT_TRUE(Record.Calls.empty());
    Declare("test.late");
    const auto token = Dispatcher->Bind(Verbs->Find("test.late"), Record);
    Sample(2, {});
    Tick(3);
    Sample(3, {Activator});
    Tick(4);
    EXPECT_EQ(Record.Calls.size(), 2u);
    EXPECT_EQ(TriggerProgramsOf(Entities).RebuildCount(), 2u);
}
TEST_F(TriggerFixture, WarmedTypedCommandsAndConditionsAllocateNothing)
{
    Settings().Event = TriggerEvent::Stay;
    auto source = Program();
    AddCondition(source);
    ASSERT_TRUE(SetTriggerProgram(Entities, Sensor, source));
    for (std::uint64_t tick = 1; tick < 20; ++tick)
    {
        Sample(tick, {Activator});
        Tick(tick + 1);
    }
    const auto before = AllocationCount();
    for (std::uint64_t tick = 20; tick < 220; ++tick)
    {
        auto &overlaps = Entities.GetResource<SensorOverlaps>();
        overlaps.Begin();
        (void)overlaps.Observe({Sensor, Activator}, true);
        (void)overlaps.Commit(tick);
        Tick(tick + 1);
    }
    const auto after = AllocationCount();
    EXPECT_EQ(after, before);
    EXPECT_GT(Record.Calls.size(), 200u);
}
TEST_F(TriggerFixture, ZoneEvictionClearsHistoryWithoutSyntheticExit)
{
    Settings().Once = true;
    Sample(1, {Activator});
    Tick(2);
    ZoneParticipation active;
    active.Logic = active.Physics = true;
    const ZoneResidencyChange change{ZoneResidencyChangeKind::ParticipationChanged, ZoneId{1},
                                     StoragePartitionId::Default(), active, ZoneParticipation{}};
    ZoneResidencyContext context{Config, Entities, std::span(&change, 1)};
    Schedule.RunZoneResidency(context);
    EXPECT_EQ(System->RetainedPairs(), 0u);
    EXPECT_EQ(Record.Calls.size(), 2u);
    Sample(2, {Activator});
    Tick(3);
    EXPECT_EQ(Record.Calls.size(), 4u);
}
TEST_F(TriggerFixture, StateCapacityNeverEvictsOnceHistory)
{
    System->SetLimits(1, 32);
    Settings().Once = true;
    const auto other = Entities.CreateEntity();
    Sample(1, {Activator, other});
    Tick(2);
    EXPECT_EQ(Record.Calls.size(), 2u);
    EXPECT_EQ(System->Stats().Overflowed, 1u);
    Sample(2, {});
    Tick(3);
    Sample(3, {Activator});
    Tick(4);
    EXPECT_EQ(Record.Calls.size(), 2u);
}
TEST_F(TriggerFixture, UnansweredQueriesFailClosed)
{
    auto source = Program();
    AddCondition(source);
    ASSERT_TRUE(SetTriggerProgram(Entities, Sensor, source));
    GateToken.Reset();
    Sample(1, {Activator});
    Tick(2);
    EXPECT_TRUE(Record.Calls.empty());
    EXPECT_EQ(System->Stats().Failed, 1u);
    EXPECT_FALSE(Trace.Snapshot().empty());
}
TEST_F(TriggerFixture, ObservationsExecuteInCompleteEntityIdentityOrder)
{
    const auto second = Entities.CreateEntity(), third = Entities.CreateEntity();
    Sample(1, {third, second, Activator});
    Tick(2);
    ASSERT_EQ(Record.Calls.size(), 6u);
    std::array<EntityId, 3> expected{Activator, second, third};
    std::ranges::sort(expected);
    for (std::size_t i = 0; i < expected.size(); ++i)
        EXPECT_EQ(Record.Calls[i * 2].Target, expected[i]);
}
TEST_F(TriggerFixture, SensorOverflowCannotInventAnExit)
{
    Settings().Event = TriggerEvent::Exit;
    Sample(1, {Activator});
    Tick(2);
    auto &overlaps = Entities.GetResource<SensorOverlaps>();
    overlaps.Begin();
    for (std::size_t i = 0; i <= overlaps.Capacity(); ++i)
        (void)overlaps.Observe({Sensor, EntityId{static_cast<EntityIndex>(i + 100), 1}}, true);
    EXPECT_FALSE(overlaps.Commit(2));
    Tick(3);
    EXPECT_TRUE(Record.Calls.empty());
    Sample(3, {Activator});
    Tick(4);
    EXPECT_TRUE(Record.Calls.empty());
    Sample(4, {});
    Tick(5);
    EXPECT_EQ(Record.Calls.size(), 2u);
}

TEST_F(TriggerFixture, ExecutionBenchmark)
{
    const char *path = std::getenv("SENCHA_TRIGGER_BENCH_OUT");
    if (path == nullptr || path[0] == '\0')
        GTEST_SKIP() << "Set SENCHA_TRIGGER_BENCH_OUT; this measures fixed-logic execution, excluding physics.";
    Bench::Recorder metrics;
    Record.Capture = false;
    const int reps = Bench::RepsFromEnvironment("SENCHA_TRIGGER_BENCH_REPS", 1000);
    std::uint64_t tick = 1;
    auto source = Program();
    AddCondition(source);
    for (const auto count : {0u, 1u, 64u, 512u, 2048u})
    {
        std::vector<SensorPair> pairs;
        std::vector<EntityId> sensors;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto sensor = Entities.CreateEntity();
            sensors.push_back(sensor);
            Entities.AddComponent<Trigger>(sensor);
            Entities.TryGet<Trigger>(sensor)->Event = TriggerEvent::Stay;
            Entities.TryGet<Trigger>(sensor)->StayInterval = 0.001f;
            ASSERT_TRUE(SetTriggerProgram(Entities, sensor, source));
            pairs.push_back({sensor, Activator});
        }
        std::vector<double> samples;
        samples.reserve(reps);
        const std::string prefix = "trigger." + std::to_string(count) + ".";
        std::size_t allocations = 0;
        const auto commandsBefore = Record.Total;
        for (int i = 0; i < 128 + reps; ++i)
        {
            auto &overlaps = Entities.GetResource<SensorOverlaps>();
            overlaps.Begin();
            for (const auto pair : pairs)
                ASSERT_TRUE(overlaps.Observe(pair, true));
            ASSERT_TRUE(overlaps.Commit(tick));
            const auto before = AllocationCount();
            const auto start = Bench::Clock::now();
            Tick(tick + 1, 1.0 / 120);
            const double ms = Bench::MillisecondsSince(start);
            if (i >= 128)
            {
                allocations += AllocationCount() - before;
                samples.push_back(ms);
            }
            ++tick;
        }
        metrics.Record(prefix + "median", "ms", Bench::Median(samples));
        metrics.Record(prefix + "p95", "ms", Bench::Percentile(samples, 0.95));
        metrics.Record(prefix + "worst", "ms", samples.back());
        metrics.Record(prefix + "allocations", "count", static_cast<double>(allocations));
        EXPECT_EQ(allocations, 0u);
        EXPECT_EQ(Record.Total - commandsBefore, static_cast<std::uint64_t>(count) * 2 * (128 + reps));
        for (const auto sensor : sensors)
            Entities.DestroyEntity(sensor);
    }
    EXPECT_TRUE(metrics.WriteJson(path));
}

TEST_F(TriggerFixture, DroppedOversizedSamplesReleaseInactiveStateWithoutSyntheticCommands)
{
    System->SetLimits(1, 32);
    Settings().Event = TriggerEvent::Stay;
    Sample(1, {Activator});
    Tick(2);
    ASSERT_EQ(Record.Calls.size(), 2u);
    const auto second = Entities.CreateEntity(), third = Entities.CreateEntity();
    Sample(2, {second, third});
    Tick(3);
    EXPECT_EQ(Record.Calls.size(), 2u);
    EXPECT_EQ(System->Stats().Overflowed, 1u);
    Sample(3, {});
    Tick(4);
    Tick(5);
    EXPECT_EQ(System->RetainedPairs(), 0u);
    Sample(5, {Activator});
    Tick(6);
    EXPECT_EQ(Record.Calls.size(), 4u);
}

TEST_F(TriggerFixture, QueryRetirementAndRevivalInvalidateCompiledPrograms)
{
    auto source = Program();
    AddCondition(source);
    ASSERT_TRUE(SetTriggerProgram(Entities, Sensor, source));
    Sample(1, {Activator});
    Tick(2);
    ASSERT_EQ(Record.Calls.size(), 2u);
    QueryCatalog->RetireProvider("test");
    Sample(2, {});
    Tick(3);
    Sample(3, {Activator});
    Tick(4);
    EXPECT_EQ(Record.Calls.size(), 2u);
    AuthoredQueryDefinition query;
    query.Name = "test.allowed";
    query.Arguments.Children.push_back(MakeDataField(DataFieldKind::Entity, "target", "Target"));
    query.Result = MakeDataField(DataFieldKind::Bool, "result", "Allowed");
    AuthoredQueryRegistrationScope scope(*QueryCatalog, "test");
    (void)scope.Declare(std::move(query));
    ASSERT_TRUE(scope.Commit());
    GateToken = QueryDispatcher->Bind<&Gate::Evaluate>(QueryCatalog->Find("test.allowed"), Condition);
    Sample(4, {});
    Tick(5);
    Sample(5, {Activator});
    Tick(6);
    EXPECT_EQ(Record.Calls.size(), 4u);
    EXPECT_EQ(TriggerProgramsOf(Entities).RebuildCount(), 3u);
}

TEST_F(TriggerFixture, PreparingProgramsDoesNotWaitForTheFirstOverlap)
{
    Tick(1);
    auto &programs = TriggerProgramsOf(Entities);
    ASSERT_EQ(programs.RebuildCount(), 1u);
    Sample(1, {Activator});
    Tick(2);
    EXPECT_EQ(Record.Calls.size(), 2u);
    EXPECT_EQ(programs.RebuildCount(), 1u);
}
