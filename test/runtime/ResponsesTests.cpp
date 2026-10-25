#include <gtest/gtest.h>

#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <assets/data/DataAssetCache.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleRegistry.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/ResponseSet.h>
#include <logic/ResponseVerbs.h>
#include <logic/Responses.h>
#include <logic/ResponsesSystem.h>
#include <physics/SensorOverlaps.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/SimClock.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZoneStateStore.h>

#include <cmath>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Entities answering occurrences delivered at them, through their own bindings,
// at a pass of a later tick. Sensor samples are published straight into
// SensorOverlaps, as the physics step would, and native occurrences are
// delivered as a game system would, so each contract is seen without a body or
// a weapon in the way.

namespace
{
// A native occurrence, as a game declares one: something struck the subject.
struct TestStruck
{
    EntityId Instigator;
    GameplayTagId Attack;
    Vec3d Point;
};

// Another: someone used the subject.
struct TestUsed
{
    EntityId User;
};

// A payload its declaration refuses: the attack given as an entity.
struct TestMisstruck
{
    EntityId Instigator;
};
} // namespace

template <>
struct AuthoredApiDefinition<TestStruck>
{
    static constexpr std::string_view EventName = "test.struck";
    static void Encode(const TestStruck& event, AuthoredArguments& payload)
    {
        payload.Resize(3);
        payload.Set(0, AuthoredValue::Entity(event.Instigator));
        payload.Set(1, AuthoredValue::Tag(event.Attack));
        payload.Set(2, AuthoredValue::Vector({ { event.Point.X, event.Point.Y, event.Point.Z }, 3 }));
    }
};

template <>
struct AuthoredApiDefinition<TestUsed>
{
    static constexpr std::string_view EventName = "test.used";
    static void Encode(const TestUsed& event, AuthoredArguments& payload)
    {
        payload.Resize(1);
        payload.Set(0, AuthoredValue::Entity(event.User));
    }
};

template <>
struct AuthoredApiDefinition<TestMisstruck>
{
    static constexpr std::string_view EventName = "test.struck";
    static void Encode(const TestMisstruck& event, AuthoredArguments& payload)
    {
        payload.Resize(3);
        payload.Set(0, AuthoredValue::Entity(event.Instigator));
        payload.Set(1, AuthoredValue::Entity(event.Instigator));
        payload.Set(2, AuthoredValue::Vector({ { 0.0, 0.0, 0.0 }, 3 }));
    }
};

namespace
{
struct RecordedCall
{
    std::string Verb;
    EntityId Target;
    double Amount = 0.0;
    AuthoredVectorValue Direction;
    EntityId Producer;
    EntityId Instigator;
    std::uint64_t Tick = 0;
};

class RecordingOperation
{
public:
    RecordingOperation(std::string verb, std::vector<RecordedCall>& calls, VerbAdmission answer)
        : Verb(std::move(verb))
        , Calls(calls)
        , Answer(answer)
    {
    }

    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        RecordedCall call;
        call.Verb = Verb;
        call.Producer = invocation.Producer;
        call.Instigator = invocation.Instigator;
        call.Tick = invocation.Tick;
        (void)invocation.Arguments->TryGetEntity(0, call.Target);
        (void)invocation.Arguments->TryGetFloat(1, call.Amount);
        (void)invocation.Arguments->TryGetVector(2, call.Direction);
        Calls.push_back(call);
        return Answer;
    }

private:
    std::string Verb;
    std::vector<RecordedCall>& Calls;
    VerbAdmission Answer;
};

// A verb whose operation strikes its target: a reaction that makes another.
class StrikingOperation
{
public:
    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        EntityId target;
        if (System == nullptr || !invocation.Arguments->TryGetEntity(0, target))
            return VerbAdmission::InvalidArguments;
        (void)System->Deliver(target, TestStruck{ invocation.Producer, Attack, {} });
        return VerbAdmission::Accepted;
    }

    ResponsesSystem* System = nullptr;
    GameplayTagId Attack;
};

// Whether the entity carries the tag Test.Red, as a query a condition can ask.
class RedQuery
{
public:
    explicit RedQuery(const World& world, GameplayTagId red)
        : Entities(world)
        , Red(red)
    {
    }

    static AuthoredQueryStatus Evaluate(const RedQuery& self, std::span<const AuthoredValue> arguments,
                                        AuthoredValue& result)
    {
        EntityId target;
        if (arguments.empty() || !arguments[0].TryGetEntity(target) || !self.Entities.IsAlive(target))
            return AuthoredQueryStatus::Unavailable;
        const GameplayTagContainer* tags = self.Entities.TryGet<GameplayTagContainer>(target);
        result = AuthoredValue::Bool(tags != nullptr && tags->HasExact(self.Red));
        return AuthoredQueryStatus::Value;
    }

private:
    const World& Entities;
    GameplayTagId Red;
};

// A record answer: how far, and which way. Exercises a value read from a
// query's member.
class ReachQuery
{
public:
    static AuthoredQueryStatus Evaluate(const ReachQuery&, std::span<const AuthoredValue>, AuthoredValue& result)
    {
        result = AuthoredValue::Record({ AuthoredValue::Float(4.5), AuthoredValue::Bool(true) });
        return AuthoredQueryStatus::Value;
    }
};

DataFieldSchema RecordArguments()
{
    DataFieldSchema root = EmptyVerbArguments();
    root.Children.push_back(MakeDataField(DataFieldKind::Entity, "target", "Target"));
    DataFieldSchema amount = MakeDataField(DataFieldKind::Float, "amount", "Amount");
    amount.Required = false;
    amount.Default = 1.0;
    root.Children.push_back(std::move(amount));
    DataFieldSchema vector = MakeDataField(DataFieldKind::Vector, "direction", "Direction");
    vector.VectorLength = 3;
    DataFieldSchema direction = MakeDataField(DataFieldKind::Optional, "direction", "Direction");
    direction.Required = false;
    direction.Children.push_back(std::move(vector));
    root.Children.push_back(std::move(direction));
    return root;
}

AuthoredEventDefinition StruckEvent(bool withDistance = false)
{
    AuthoredEventDefinition struck;
    struck.Name = "test.struck";
    struck.DisplayName = "Struck";
    struck.Payload.Children.push_back(MakeDataField(DataFieldKind::Entity, "instigator", "Instigator"));
    struck.Payload.Children.push_back(MakeDataField(DataFieldKind::GameplayTag, "attack", "Attack"));
    DataFieldSchema point = MakeDataField(DataFieldKind::Vector, "point", "Point");
    point.VectorLength = 3;
    struck.Payload.Children.push_back(std::move(point));
    if (withDistance)
        struck.Payload.Children.push_back(MakeDataField(DataFieldKind::Float, "distance", "Distance"));
    struck.Instigator = "instigator";
    return struck;
}

JsonValue Json(std::string_view text)
{
    std::optional<JsonValue> parsed = JsonParse(text);
    EXPECT_TRUE(parsed.has_value()) << text;
    return parsed.value_or(JsonValue());
}

// One binding's text: "key": {"on": ..., "sequence": ..., policy...}.
std::string Binding(std::string_view key, std::string_view on, std::string_view sequence,
                    std::string_view policy = {})
{
    std::string text = "\"" + std::string(key) + "\": {\"on\": \"" + std::string(on) + "\", \"sequence\": "
                     + std::string(sequence);
    if (!policy.empty())
        text += ", " + std::string(policy);
    return text + "}";
}

std::string Bindings(std::initializer_list<std::string> bindings)
{
    std::string text = "{";
    for (const std::string& binding : bindings)
        text += (text.size() > 1 ? ", " : "") + binding;
    return text + "}";
}

constexpr std::string_view kRecordActivator = R"({"commands": [
    {"verb": "test.record", "arguments": {"target": {"port": "activator"}}}]})";
constexpr std::string_view kRecordSelf = R"({"commands": [
    {"verb": "test.record", "arguments": {"target": {"port": "self"}}}]})";
constexpr std::string_view kRecordInstigator = R"({"commands": [
    {"verb": "test.record", "arguments": {"target": {"port": "instigator"}}}]})";

class ResponsesFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ComponentRegistrar registrar(WorldState);
        registrar.Add<Responses>();
        registrar.Add<GameplayTagContainer>();
        registrar.Add<LocalTransform>();
        registrar.Add<WorldTransform>();
        registrar.Add<PersistentIdComponent>();
        WorldState.AddResource<PersistentEntityIndex>();
        ZoneState = &WorldState.AddResource<ZoneStateStore>();

        Tags = &WorldState.AddResource<GameplayTagRegistry>();
        Red = *Tags->RegisterTag("Test.Red");
        Charge = *Tags->RegisterTag("Test.Attack.Charge");
        Strong = *Tags->RegisterTag("Test.Attack.Charge.Strong");
        Default = *Tags->RegisterTag("Test.Attack.Default");

        InstallAuthoredVocabulary(WorldState);
        Verbs = FindVerbRegistry(WorldState);
        {
            VerbRegistrationScope scope(*Verbs, "test");
            AuthoredEventRegistrationScope events(*FindAuthoredEventRegistry(WorldState), "test");
            for (const char* name : { "test.record", "test.second", "test.refuse", "test.strike" })
            {
                VerbDefinition verb;
                verb.Name = name;
                verb.Arguments = RecordArguments();
                (void)scope.Declare(std::move(verb));
            }
            DeclareResponseVocabulary(scope, events);
            (void)events.Declare(StruckEvent());
            AuthoredEventDefinition used;
            used.Name = "test.used";
            used.Payload.Children.push_back(MakeDataField(DataFieldKind::Entity, "user", "User"));
            used.Instigator = "user";
            (void)events.Declare(std::move(used));
            ASSERT_TRUE(CommitTogether(scope, events));
        }
        {
            AuthoredQueryRegistrationScope scope(*FindAuthoredQueryRegistry(WorldState), "test");
            AuthoredQueryDefinition red;
            red.Name = "test.is_red";
            red.Arguments.Children.push_back(MakeDataField(DataFieldKind::Entity, "target", "Target"));
            red.Result = MakeDataField(DataFieldKind::Bool, "result", "Red");
            (void)scope.Declare(std::move(red));
            AuthoredQueryDefinition reach;
            reach.Name = "test.reach";
            reach.Result = AuthoredRecordRoot();
            reach.Result.Children.push_back(MakeDataField(DataFieldKind::Float, "distance", "Distance"));
            reach.Result.Children.push_back(MakeDataField(DataFieldKind::Bool, "clear", "Clear"));
            (void)scope.Declare(std::move(reach));
            ASSERT_TRUE(scope.Commit());
        }

        Dispatcher = std::make_unique<VerbDispatcher>(*Verbs);
        Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(WorldState));
        Record = std::make_unique<RecordingOperation>("test.record", Calls, VerbAdmission::Accepted);
        Second = std::make_unique<RecordingOperation>("test.second", Calls, VerbAdmission::Accepted);
        Refuse = std::make_unique<RecordingOperation>("test.refuse", Calls, VerbAdmission::Refused);
        Operations = std::make_unique<ResponseOperations>(WorldState, Loop);
        RedAnswer = std::make_unique<RedQuery>(WorldState, Red);
        Tokens.push_back(Dispatcher->Bind(Verbs->Find("test.record"), *Record));
        Tokens.push_back(Dispatcher->Bind(Verbs->Find("test.second"), *Second));
        Tokens.push_back(Dispatcher->Bind(Verbs->Find("test.refuse"), *Refuse));
        Tokens.push_back(Dispatcher->Bind(Verbs->Find("test.strike"), Strike));
        Tokens.push_back(Dispatcher->Bind<&InvokeResponsesEnable>(Verbs->Find(kResponsesEnableVerb), *Operations));
        Tokens.push_back(Dispatcher->Bind<&InvokeResponsesDisable>(Verbs->Find(kResponsesDisableVerb), *Operations));
        Tokens.push_back(Dispatcher->Bind<&InvokeTriggerInteract>(Verbs->Find(kTriggerInteractVerb), *Operations));
        QueryToken = Queries->Bind<&RedQuery::Evaluate>(FindAuthoredQueryRegistry(WorldState)->Find("test.is_red"),
                                                       *RedAnswer);
        ReachToken = Queries->Bind<&ReachQuery::Evaluate>(FindAuthoredQueryRegistry(WorldState)->Find("test.reach"),
                                                         Reach);

        Overlaps = &WorldState.AddResource<SensorOverlaps>();
        Logic.Add(StoragePartitionId::Default());
        System = &RegisterResponsesSystem(Schedule, WorldState, Loop, *Dispatcher, Queries.get(), nullptr, nullptr,
                                          Console, Logging);
        Strike.System = System;
        Strike.Attack = Charge;
        Schedule.Init();
    }

    void TearDown() override
    {
        Schedule.Shutdown();
        QueryToken.Reset();
        ReachToken.Reset();
        for (VerbBindingToken& token : Tokens)
            token.Reset();
    }

    EntityId Place(const std::string& bindings, StoragePartitionId partition = StoragePartitionId::Default())
    {
        const EntityId entity = WorldState.CreateEntity(partition);
        WorldState.AddComponent<Responses>(entity, Responses{});
        EXPECT_TRUE(SetResponseBindings(WorldState, entity, Json(bindings)));
        return entity;
    }

    // One sensor binding answering `on`, keyed "b".
    EntityId PlaceSensor(std::string_view on, std::string_view sequence, std::string_view policy = {},
                         StoragePartitionId partition = StoragePartitionId::Default())
    {
        return Place(Bindings({ Binding("b", on, sequence, policy) }), partition);
    }

    const Responses& StateOf(EntityId entity) { return *WorldState.TryGet<Responses>(entity); }

    // The physics step's sample at the current tick: these pairs are occupied.
    void Sample(std::initializer_list<SensorPair> occupied)
    {
        Overlaps->Begin();
        for (const SensorPair& pair : occupied)
            EXPECT_TRUE(Overlaps->Observe(pair, true));
        EXPECT_TRUE(Overlaps->Commit(TickIndex));
    }

    // The clock moves to the next fixed tick; producers that run now run
    // before the response pass does.
    void Advance()
    {
        Loop.EndFixedTick();
        ++TickIndex;
        ASSERT_EQ(Loop.GetTickIndex(), TickIndex);
    }

    void RunPass()
    {
        FixedLogicContext fixed{
            .Config = Config,
            .Runtime = Runtime,
            .Time = FixedSimTime{ .DeltaSeconds = 0.1, .TickIndex = TickIndex },
            .Entities = WorldState,
            .Partitions = Logic,
        };
        Schedule.RunFixedLogic(fixed);
    }

    void Tick()
    {
        Advance();
        RunPass();
    }

    // A sample, then the pass that reads it.
    void Step(std::initializer_list<SensorPair> occupied)
    {
        Sample(occupied);
        Tick();
    }

    void Residency(ZoneResidencyChangeKind kind, StoragePartitionId partition, ZoneId zone = ZoneId{ 1 })
    {
        ZoneResidencyChange change;
        change.Kind = kind;
        change.Zone = zone;
        change.Partition = partition;
        ZoneResidencyContext residency{ .Config = Config, .Entities = WorldState, .Changes = { &change, 1 } };
        Schedule.RunZoneResidency(residency);
    }

    VerbAdmission SetEnabled(EntityId entity, std::string_view binding, bool enabled)
    {
        VerbInvocation request;
        AuthoredArguments arguments(2);
        arguments.Set(0, AuthoredValue::Entity(entity));
        arguments.Set(1, AuthoredValue::String(std::string(binding)));
        request.Arguments = &arguments;
        return enabled ? Operations->Enable(request) : Operations->Disable(request);
    }

    LoggingProvider Logging;
    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    ConsoleRegistry Console;
    World WorldState;
    StoragePartitionSet Logic;
    EngineSchedule Schedule;
    FixedSimulationLoop Loop{ 0.1 };
    ZoneStateStore* ZoneState = nullptr;
    GameplayTagRegistry* Tags = nullptr;
    GameplayTagId Red;
    GameplayTagId Charge;
    GameplayTagId Strong;
    GameplayTagId Default;
    VerbRegistry* Verbs = nullptr;
    std::unique_ptr<VerbDispatcher> Dispatcher;
    std::unique_ptr<AuthoredQueryDispatcher> Queries;
    std::vector<RecordedCall> Calls;
    std::unique_ptr<RecordingOperation> Record;
    std::unique_ptr<RecordingOperation> Second;
    std::unique_ptr<RecordingOperation> Refuse;
    StrikingOperation Strike;
    std::unique_ptr<ResponseOperations> Operations;
    std::unique_ptr<RedQuery> RedAnswer;
    std::vector<VerbBindingToken> Tokens;
    AuthoredQueryBindingToken QueryToken;
    ReachQuery Reach;
    AuthoredQueryBindingToken ReachToken;
    SensorOverlaps* Overlaps = nullptr;
    ResponsesSystem* System = nullptr;
    std::uint64_t TickIndex = 0;
};
} // namespace

// ---------------------------------------------------------------------------
// Sensor volumes: the first producer, unchanged in what a mapper sees.
// ---------------------------------------------------------------------------

TEST_F(ResponsesFixture, EnterRunsTheSequenceAtThePassAfterTheSampleWithProvenance)
{
    const EntityId volume = PlaceSensor(kSensorEnterEvent, kRecordActivator);
    const EntityId activator = WorldState.CreateEntity();

    Sample({ { volume, activator } });
    EXPECT_TRUE(Calls.empty()) << "a sequence ran inside the physics sample";
    Tick();
    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_EQ(Calls[0].Target, activator);
    EXPECT_EQ(Calls[0].Producer, volume);
    EXPECT_EQ(Calls[0].Instigator, activator) << "the sensor event names its activator as instigator";
    EXPECT_EQ(Calls[0].Tick, TickIndex);

    // Staying inside is not entering again.
    Step({ { volume, activator } });
    Step({ { volume, activator } });
    EXPECT_EQ(Calls.size(), 1u);
}

TEST_F(ResponsesFixture, AFastCrossingStillEntersAndExits)
{
    const EntityId enter = PlaceSensor(kSensorEnterEvent, kRecordActivator);
    const EntityId exit = PlaceSensor(kSensorExitEvent, kRecordActivator);
    const EntityId activator = WorldState.CreateEntity();

    // A cast through both volumes with neither endpoint inside.
    Overlaps->Begin();
    ASSERT_TRUE(Overlaps->Observe({ enter, activator }, false));
    ASSERT_TRUE(Overlaps->Observe({ exit, activator }, false));
    ASSERT_TRUE(Overlaps->Commit(TickIndex));
    Tick();

    ASSERT_EQ(Calls.size(), 2u);
    EXPECT_EQ(Calls[0].Producer, enter);
    EXPECT_EQ(Calls[1].Producer, exit);
}

TEST_F(ResponsesFixture, ExitRunsWhenTheActivatorLeaves)
{
    const EntityId volume = PlaceSensor(kSensorExitEvent, kRecordActivator);
    const EntityId activator = WorldState.CreateEntity();

    Step({ { volume, activator } });
    Step({ { volume, activator } });
    EXPECT_TRUE(Calls.empty());
    Step({});
    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_EQ(Calls[0].Target, activator);
}

// Stay is answered every sample inside, entry included; an instigator's
// cooldown sets the cadence. Coming back inside the cooldown does not run
// again -- the one place this differs from the old stay interval, which reset
// on re-entry.
TEST_F(ResponsesFixture, StayRunsOnEntryThenOncePerCooldownForEachActivator)
{
    // Ticks are a tenth of a second: every 0.3 s is every third pass.
    const EntityId volume =
        PlaceSensor(kSensorStayEvent, kRecordActivator, R"("cooldown": 0.3, "cooldown_scope": "instigator")");
    const EntityId activator = WorldState.CreateEntity();

    for (int i = 0; i < 7; ++i)
        Step({ { volume, activator } });
    EXPECT_EQ(Calls.size(), 3u) << "entry, then two cooldowns";

    Step({});
    Step({ { volume, activator } });
    EXPECT_EQ(Calls.size(), 3u) << "re-entry inside the cooldown ran";
    Step({ { volume, activator } });
    EXPECT_EQ(Calls.size(), 4u);
}

TEST_F(ResponsesFixture, ManyActivatorsRunInIdentityOrderAndCommandsInAuthoredOrder)
{
    const EntityId volume = PlaceSensor(kSensorEnterEvent, R"({"commands": [
        {"verb": "test.record", "arguments": {"target": {"port": "activator"}}},
        {"verb": "test.second", "arguments": {"target": {"port": "activator"}}}]})");
    const EntityId first = WorldState.CreateEntity();
    const EntityId second = WorldState.CreateEntity();

    Step({ { volume, second }, { volume, first } });

    ASSERT_EQ(Calls.size(), 4u);
    const EntityId earlier = first < second ? first : second;
    const EntityId later = first < second ? second : first;
    EXPECT_EQ(Calls[0].Target, earlier);
    EXPECT_EQ(Calls[0].Verb, "test.record");
    EXPECT_EQ(Calls[1].Target, earlier);
    EXPECT_EQ(Calls[1].Verb, "test.second");
    EXPECT_EQ(Calls[2].Target, later);
    EXPECT_EQ(Calls[3].Target, later);
}

TEST_F(ResponsesFixture, AFailedCommandContinuesOrStopsAsItsPolicySays)
{
    const EntityId continues = PlaceSensor(kSensorEnterEvent, R"({"commands": [
        {"verb": "test.refuse", "arguments": {"target": {"port": "activator"}}},
        {"verb": "test.record", "arguments": {"target": {"port": "activator"}}}]})");
    const EntityId stops = PlaceSensor(kSensorEnterEvent, R"({"commands": [
        {"verb": "test.refuse", "arguments": {"target": {"port": "activator"}}, "on_failure": "stop"},
        {"verb": "test.record", "arguments": {"target": {"port": "activator"}}}]})");
    const EntityId activator = WorldState.CreateEntity();

    Step({ { continues, activator } });
    ASSERT_EQ(Calls.size(), 2u);
    EXPECT_EQ(Calls[1].Verb, "test.record");

    Calls.clear();
    Step({});
    Step({ { stops, activator } });
    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_EQ(Calls[0].Verb, "test.refuse");
    EXPECT_EQ(System->Stats().Failed, 2u);
}

TEST_F(ResponsesFixture, AnUnknownVerbFailsItsCommandAndTheRestStillRun)
{
    const EntityId volume = PlaceSensor(kSensorEnterEvent, R"({"commands": [
        {"verb": "test.missing", "arguments": {"target": {"port": "activator"}}},
        {"verb": "test.record", "arguments": {"target": {"port": "nowhere"}}},
        {"verb": "test.record", "arguments": {"target": {"port": "activator"}, "amount": 2}}]})");
    const EntityId activator = WorldState.CreateEntity();

    Step({ { volume, activator } });

    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_DOUBLE_EQ(Calls[0].Amount, 2.0);
    EXPECT_EQ(System->Stats().Failed, 2u);
    const ResponseDefinitions& definitions = ResponseDefinitionsOf(WorldState);
    const VerbSequenceKey sequence = definitions.Sets.Bindings(StateOf(volume).Set).front().Sequence;
    const std::span<const std::string> diagnostics = definitions.Sequences.Diagnostics(sequence);
    ASSERT_EQ(diagnostics.size(), 2u);
    EXPECT_NE(diagnostics[0].find("test.missing"), std::string::npos);
    EXPECT_NE(diagnostics[1].find("nowhere"), std::string::npos);
}

TEST_F(ResponsesFixture, OnceConsumesTheBindingAndTheEntityStays)
{
    const EntityId volume = PlaceSensor(kSensorEnterEvent, kRecordActivator, R"("once": true)");
    const EntityId first = WorldState.CreateEntity();
    const EntityId second = WorldState.CreateEntity();

    Step({ { volume, first }, { volume, second } });

    EXPECT_EQ(Calls.size(), 1u) << "a once binding ran for a second activator in its own pass";
    EXPECT_TRUE(WorldState.IsAlive(volume)) << "running once ended the entity";
    EXPECT_EQ(StateOf(volume).Consumed, 1u);

    Step({});
    Step({ { volume, first } });
    EXPECT_EQ(Calls.size(), 1u);
}

TEST_F(ResponsesFixture, ABindingCooldownHoldsEveryoneAndAnInstigatorCooldownOnlyThatOne)
{
    const EntityId shared = PlaceSensor(kSensorEnterEvent, kRecordActivator, R"("cooldown": 0.5)");
    const EntityId own =
        PlaceSensor(kSensorEnterEvent, kRecordActivator, R"("cooldown": 0.5, "cooldown_scope": "instigator")");
    const EntityId a = WorldState.CreateEntity();
    const EntityId b = WorldState.CreateEntity();

    Step({ { shared, a }, { own, a } });
    ASSERT_EQ(Calls.size(), 2u);
    Calls.clear();

    // B arrives while A's activation cools: only the per-instigator binding runs.
    Step({ { shared, a }, { shared, b }, { own, a }, { own, b } });
    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_EQ(Calls[0].Producer, own);
    EXPECT_EQ(Calls[0].Target, b);
    Calls.clear();

    // A re-enters after its cooldown: both run again.
    for (int i = 0; i < 5; ++i)
        Step({});
    Step({ { shared, a }, { own, a } });
    EXPECT_EQ(Calls.size(), 2u);
}

TEST_F(ResponsesFixture, ADisabledBindingWaitsForEnable)
{
    const EntityId volume = PlaceSensor(kSensorEnterEvent, kRecordActivator, R"("enabled": false)");
    const EntityId activator = WorldState.CreateEntity();
    EXPECT_EQ(StateOf(volume).Disabled, 1u) << "authored disabled did not load disabled";

    Step({ { volume, activator } });
    EXPECT_TRUE(Calls.empty());

    EXPECT_EQ(SetEnabled(volume, "b", true), VerbAdmission::Accepted);
    EXPECT_EQ(StateOf(volume).Disabled, 0u);
    EXPECT_EQ(SetEnabled(volume, "missing", true), VerbAdmission::InvalidArguments);

    Step({ { volume, activator } });
    EXPECT_TRUE(Calls.empty()) << "enabling ran for an activator already inside";
    Step({});
    Step({ { volume, activator } });
    EXPECT_EQ(Calls.size(), 1u);
}

TEST_F(ResponsesFixture, ConditionsFilterTheActivatorByComponentTagAndQuery)
{
    const EntityId byTag = PlaceSensor(kSensorEnterEvent, R"({
        "conditions": [{"entity": "activator", "has": ["sencha.gameplay_tag_container"], "tags": {"all": ["Test.Red"]}}],
        "commands": [{"verb": "test.record", "arguments": {"target": {"port": "activator"}}}]})");
    const EntityId byQuery = PlaceSensor(kSensorEnterEvent, R"({
        "conditions": [{"query": "test.is_red", "arguments": {"target": {"port": "activator"}}, "expect": false}],
        "commands": [{"verb": "test.second", "arguments": {"target": {"port": "activator"}}}]})");
    const EntityId red = WorldState.CreateEntity();
    GameplayTagContainer tags;
    tags.Grant(Red);
    WorldState.AddComponent<GameplayTagContainer>(red, tags);
    const EntityId plain = WorldState.CreateEntity();
    WorldState.AddComponent<GameplayTagContainer>(plain, GameplayTagContainer{});
    const EntityId bare = WorldState.CreateEntity();

    Step({ { byTag, red }, { byTag, plain }, { byTag, bare }, { byQuery, red }, { byQuery, plain } });

    ASSERT_EQ(Calls.size(), 2u);
    EXPECT_EQ(Calls[0].Verb, "test.record");
    EXPECT_EQ(Calls[0].Target, red);
    EXPECT_EQ(Calls[1].Verb, "test.second");
    EXPECT_EQ(Calls[1].Target, plain);
    EXPECT_EQ(System->Stats().Gated, 3u);
}

TEST_F(ResponsesFixture, AValueMayBeGivenInTheEntitysFrameAndScaled)
{
    const EntityId volume = PlaceSensor(kSensorEnterEvent, R"({"commands": [
        {"verb": "test.record", "arguments": {
            "target": {"port": "activator"},
            "amount": {"const": 2, "scale": 3},
            "direction": {"const": [0, 0, 1], "frame": "self", "scale": 2}}}]})");
    // Turned a quarter about up: its forward (+Z) is world +X.
    WorldTransform pose;
    pose.Value.Rotation = Quatf::FromAxisAngle(Vec3d(0.0f, 1.0f, 0.0f), 1.57079633f);
    WorldState.AddComponent<WorldTransform>(volume, pose);
    const EntityId activator = WorldState.CreateEntity();

    Step({ { volume, activator } });

    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_DOUBLE_EQ(Calls[0].Amount, 6.0);
    EXPECT_NEAR(Calls[0].Direction.Components[0], 2.0, 1e-5);
    EXPECT_NEAR(Calls[0].Direction.Components[1], 0.0, 1e-5);
    EXPECT_NEAR(Calls[0].Direction.Components[2], 0.0, 1e-5);
}

// An activator is payload, captured with the occurrence: one that ends before
// the pass reaches the response as a stale id. A condition on it fails, which
// is what keeps a bumper from launching what is gone; a verb handed it refuses.
TEST_F(ResponsesFixture, AnActivatorThatEndsBeforeThePassFailsConditionsOnIt)
{
    const EntityId guarded = PlaceSensor(kSensorEnterEvent, R"({
        "conditions": [{"entity": "activator"}],
        "commands": [{"verb": "test.record", "arguments": {"target": {"port": "activator"}}}]})");
    const EntityId activator = WorldState.CreateEntity();

    Sample({ { guarded, activator } });
    WorldState.DestroyEntity(activator);
    const EntityId reused = WorldState.CreateEntity();
    Tick();

    EXPECT_TRUE(Calls.empty());
    EXPECT_EQ(System->Stats().Gated, 1u);
    EXPECT_NE(reused, activator) << "the stale id must not name the entity that reused its slot";
}

TEST_F(ResponsesFixture, ADormantZonesActivationsAreDroppedNotRunLater)
{
    const StoragePartitionId zone{ 7 };
    const EntityId volume = PlaceSensor(kSensorEnterEvent, kRecordActivator, {}, zone);
    const EntityId activator = WorldState.CreateEntity();

    // The zone does not participate in logic this pass.
    Step({ { volume, activator } });
    EXPECT_TRUE(Calls.empty());
    EXPECT_EQ(System->Pending(), 0u) << "an activation waited for a dormant zone";
    EXPECT_EQ(System->Stats().Abandoned, 1u);

    Logic.Add(zone);
    Step({ { volume, activator } });
    EXPECT_TRUE(Calls.empty()) << "a stale entry ran when its zone woke";
}

TEST_F(ResponsesFixture, AClientRunsNothing)
{
    WorldState.AddResource<SimulationAuthority>().Authoritative = false;
    const EntityId volume = PlaceSensor(kSensorEnterEvent, kRecordActivator);
    const EntityId target = Place(Bindings({ Binding("hit", "test.struck", kRecordSelf) }));
    const EntityId activator = WorldState.CreateEntity();

    Step({ { volume, activator } });
    EXPECT_EQ(System->Deliver(target, TestStruck{ activator, Charge, {} }), 0u) << "a client admitted a delivery";
    Tick();
    EXPECT_TRUE(Calls.empty());
    EXPECT_EQ(System->Pending(), 0u);

    // Becoming the authority later does not replay what the client saw.
    WorldState.GetResource<SimulationAuthority>().Authoritative = true;
    Step({ { volume, activator } });
    EXPECT_TRUE(Calls.empty());
}

TEST_F(ResponsesFixture, AnInteractionRunsTheVolumesTheActivatorStandsIn)
{
    const EntityId inside = PlaceSensor(kSensorInteractEvent, kRecordActivator);
    const EntityId elsewhere = PlaceSensor(kSensorInteractEvent, kRecordActivator);
    const EntityId activator = WorldState.CreateEntity();
    const EntityId other = WorldState.CreateEntity();

    Step({ { inside, activator }, { elsewhere, other } });
    EXPECT_TRUE(Calls.empty()) << "standing in an interaction volume activated it";

    VerbInvocation request;
    AuthoredArguments arguments(1);
    arguments.Set(0, AuthoredValue::Entity(activator));
    request.Arguments = &arguments;
    EXPECT_EQ(Operations->Interact(request), VerbAdmission::Accepted);
    Step({ { inside, activator }, { elsewhere, other } });

    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_EQ(Calls[0].Producer, inside);
    EXPECT_EQ(Calls[0].Target, activator);
}

TEST_F(ResponsesFixture, ActivationsPastTheBudgetWaitForTheNextPassInOrder)
{
    System->SetCommandBudget(2);
    const EntityId volume = PlaceSensor(kSensorEnterEvent, kRecordActivator);
    std::vector<EntityId> activators;
    for (int i = 0; i < 5; ++i)
        activators.push_back(WorldState.CreateEntity());

    Overlaps->Begin();
    for (const EntityId activator : activators)
        ASSERT_TRUE(Overlaps->Observe({ volume, activator }, true));
    ASSERT_TRUE(Overlaps->Commit(TickIndex));
    Tick();
    EXPECT_EQ(Calls.size(), 2u);
    EXPECT_EQ(System->Pending(), 3u);
    Tick();
    Tick();
    ASSERT_EQ(Calls.size(), 5u);
    for (std::size_t i = 0; i < activators.size(); ++i)
        EXPECT_EQ(Calls[i].Target, activators[i]);
    EXPECT_EQ(System->Stats().Refused, 0u) << "the budget dropped admitted work";
}

TEST_F(ResponsesFixture, ASetKeyIsAValueThatSurvivesCopiesOfTheComponent)
{
    const std::string text = Bindings({ Binding("b", kSensorEnterEvent, kRecordActivator) });
    const EntityId a = Place(text);
    const EntityId b = Place(text);
    EXPECT_EQ(StateOf(a).Set, StateOf(b).Set) << "entities authored alike did not share one set";
    EXPECT_EQ(ResponseDefinitionsOf(WorldState).Sets.Size(), 1u);

    // The component's bytes, kept as an undo step keeps them, outlive a change
    // to its bindings and the end of every other holder.
    const Responses kept = StateOf(a);
    ASSERT_TRUE(SetResponseBindings(WorldState, a, EmptyResponseBindings()));
    WorldState.DestroyEntity(b);
    *WorldState.TryGet<Responses>(a) = kept;
    const JsonValue* restored = ResponseBindingsSource(WorldState, a);
    ASSERT_NE(restored, nullptr) << "a restored key resolved to nothing";
    EXPECT_EQ(JsonStringify(*restored), JsonStringify(Json(text)));
}

TEST_F(ResponsesFixture, ADataAssetThatArrivesLaterResolvesTheCommandNamingIt)
{
    {
        VerbRegistrationScope scope(*Verbs, "data");
        VerbDefinition use;
        use.Name = "test.use";
        DataFieldSchema thing = MakeDataField(DataFieldKind::DataAssetRef, "thing", "Thing");
        thing.Reference.DataSubtype = "test.thing";
        use.Arguments.Children.push_back(std::move(thing));
        (void)scope.Declare(std::move(use));
        ASSERT_TRUE(scope.Commit());
    }
    VerbSequenceStore& store = ResponseDefinitionsOf(WorldState).Sequences;
    const VerbSequenceKey key = store.Intern(Json(R"({"commands": [
        {"verb": "test.use", "arguments": {"thing": {"data": "asset://data/thing.sdata"}}}]})"));
    DataAssetCache cache;

    store.Revalidate(WorldState);
    const CompiledVerbSequence* before = store.Resolve(key, WorldState, nullptr, &cache);
    ASSERT_NE(before, nullptr);
    EXPECT_FALSE(before->Commands.front().Resolved) << "a command compiled against an asset that was not resident";
    const std::uint64_t compiles = store.CompileCount();
    (void)store.Resolve(key, WorldState, nullptr, &cache);
    EXPECT_EQ(store.CompileCount(), compiles) << "nothing moved, and the sequence compiled again";

    const DataAssetHandle thing =
        cache.Register("asset://data/thing.sdata", "test.thing", std::make_shared<const int>(1));
    ASSERT_TRUE(thing.IsValid());
    const CompiledVerbSequence* after = store.Resolve(key, WorldState, nullptr, &cache);
    ASSERT_NE(after, nullptr);
    EXPECT_TRUE(after->Commands.front().Resolved) << "the asset arrived and the sequence did not compile again";
}

TEST_F(ResponsesFixture, AValueMayBeAQuerysAnswerOrOneMemberOfIt)
{
    const EntityId volume = PlaceSensor(kSensorEnterEvent, R"({"commands": [
        {"verb": "test.record", "arguments": {
            "target": {"port": "activator"},
            "amount": {"query": "test.reach", "member": "distance", "scale": 2}}}]})");
    const EntityId activator = WorldState.CreateEntity();

    Step({ { volume, activator } });

    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_DOUBLE_EQ(Calls[0].Amount, 9.0);
}

TEST_F(ResponsesFixture, AnAnswerOfTheWrongKindIsRefusedWhenItCompiles)
{
    const EntityId volume = PlaceSensor(kSensorEnterEvent, R"({"commands": [
        {"verb": "test.record", "arguments": {
            "target": {"port": "activator"},
            "amount": {"query": "test.reach", "member": "clear"}}}]})");
    const EntityId activator = WorldState.CreateEntity();

    Step({ { volume, activator } });

    EXPECT_TRUE(Calls.empty());
    EXPECT_EQ(System->Stats().Failed, 1u);
    const ResponseDefinitions& definitions = ResponseDefinitionsOf(WorldState);
    const auto diagnostics =
        definitions.Sequences.Diagnostics(definitions.Sets.Bindings(StateOf(volume).Set).front().Sequence);
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_NE(diagnostics[0].find("different kind"), std::string::npos) << diagnostics[0];
}

TEST_F(ResponsesFixture, AZoneThatUnloadsTakesItsWaitingActivationsWithIt)
{
    System->SetCommandBudget(1);
    const StoragePartitionId zone{ 3 };
    Logic.Add(zone);
    const EntityId first = PlaceSensor(kSensorEnterEvent, kRecordActivator, {}, zone);
    const EntityId second = PlaceSensor(kSensorEnterEvent, kRecordActivator, {}, zone);
    const EntityId activator = WorldState.CreateEntity();

    Step({ { first, activator }, { second, activator } });
    ASSERT_EQ(Calls.size(), 1u);
    ASSERT_EQ(System->Pending(), 1u);

    Residency(ZoneResidencyChangeKind::Detaching, zone);
    EXPECT_EQ(System->Pending(), 0u);
    Tick();
    EXPECT_EQ(Calls.size(), 1u) << "an activation ran for a zone that had gone";
}

// ---------------------------------------------------------------------------
// Targeted delivery: any native producer, through the same machinery.
// ---------------------------------------------------------------------------

TEST_F(ResponsesFixture, DeliverRunsOnlyTheSubjectsBindingsWithTheEventsPorts)
{
    const std::string bindings = Bindings({ Binding("hit", "test.struck", R"({"commands": [
        {"verb": "test.record", "arguments": {"target": {"port": "self"}, "direction": {"port": "point"}}}]})") });
    const EntityId struck = Place(bindings);
    const EntityId bystander = Place(bindings);
    const EntityId shooter = WorldState.CreateEntity();

    Tick();
    EXPECT_EQ(System->Deliver(struck, TestStruck{ shooter, Charge, Vec3d(1.0f, 2.0f, 3.0f) }), 1u);
    Tick();

    ASSERT_EQ(Calls.size(), 1u) << "a delivery reached an entity it was not delivered to";
    EXPECT_EQ(Calls[0].Target, struck);
    EXPECT_EQ(Calls[0].Producer, struck) << "the response's provenance is the entity it ran on";
    EXPECT_EQ(Calls[0].Instigator, shooter) << "the event's declared instigator";
    EXPECT_NEAR(Calls[0].Direction.Components[1], 2.0, 1e-5);
    (void)bystander;
}

// When a producer runs within the tick decides nothing: whichever side of the
// response pass it runs on, its occurrence is answered at the next tick's pass.
TEST_F(ResponsesFixture, AnOccurrenceRunsAtTheNextTicksPassWhicheverSideOfThePassItsProducerRuns)
{
    const EntityId struck = Place(Bindings({ Binding("hit", "test.struck", kRecordSelf) }));
    const EntityId shooter = WorldState.CreateEntity();

    // Before the pass, in tick N.
    Advance();
    const std::uint64_t before = TickIndex;
    ASSERT_EQ(System->Deliver(struck, TestStruck{ shooter, Charge, {} }), 1u);
    RunPass();
    EXPECT_TRUE(Calls.empty()) << "an occurrence ran in the pass of the tick it was delivered in";
    Tick();
    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_EQ(Calls[0].Tick, before + 1);

    // After the pass, in tick N.
    Calls.clear();
    const std::uint64_t after = TickIndex;
    ASSERT_EQ(System->Deliver(struck, TestStruck{ shooter, Charge, {} }), 1u);
    Tick();
    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_EQ(Calls[0].Tick, after + 1);
}

TEST_F(ResponsesFixture, AnEntityWithoutResponsesCostsAValidityCheckAndOneBit)
{
    const EntityId wall = WorldState.CreateEntity();
    const EntityId other = Place(Bindings({ Binding("used", "test.used", kRecordSelf) }));
    const EntityId shooter = WorldState.CreateEntity();

    EXPECT_EQ(System->Deliver(EntityId{}, TestStruck{ shooter, Charge, {} }), 0u);
    EXPECT_EQ(System->Deliver(wall, TestStruck{ shooter, Charge, {} }), 0u);
    EXPECT_EQ(System->Stats().Delivered, 0u) << "a delivery to an entity without responses went past the bit";
    EXPECT_EQ(System->Stats().Encoded, 0u);

    // Responses, but none to this event: no encoding, nothing queued.
    EXPECT_EQ(System->Deliver(other, TestStruck{ shooter, Charge, {} }), 0u);
    EXPECT_EQ(System->Stats().Delivered, 1u);
    EXPECT_EQ(System->Stats().Encoded, 0u);
    EXPECT_EQ(System->Pending(), 0u);
}

TEST_F(ResponsesFixture, AnEntityAnswersSeveralOccurrencesEachWithItsOwnPolicy)
{
    // One switch: a charge shot opens (once), a use toggles (with a cooldown),
    // and walking into it records the walker -- no child entities.
    const EntityId switchEntity = Place(Bindings({
        Binding("shot", "test.struck", R"({
            "conditions": [{"tag": "attack", "tags": {"any": ["Test.Attack.Charge"]}}],
            "commands": [{"verb": "test.record", "arguments": {"target": {"port": "self"}}}]})", R"("once": true)"),
        Binding("use", "test.used", R"({"commands": [{"verb": "test.second", "arguments": {"target": {"port": "user"}}}]})",
                R"("cooldown": 1.0)"),
        Binding("walk", kSensorEnterEvent, kRecordActivator),
    }));
    const EntityId player = WorldState.CreateEntity();

    ASSERT_EQ(System->Deliver(switchEntity, TestStruck{ player, Default, {} }), 1u);
    ASSERT_EQ(System->Deliver(switchEntity, TestUsed{ player }), 1u);
    Step({ { switchEntity, player } });
    ASSERT_EQ(Calls.size(), 2u) << "the default shot failed its condition; use and walk ran";
    EXPECT_EQ(Calls[0].Verb, "test.second");
    EXPECT_EQ(Calls[1].Target, player);

    ASSERT_EQ(System->Deliver(switchEntity, TestStruck{ player, Charge, {} }), 1u);
    ASSERT_EQ(System->Deliver(switchEntity, TestUsed{ player }), 0u) << "use admitted while cooling";
    Tick();
    ASSERT_EQ(Calls.size(), 3u);
    EXPECT_EQ(Calls[2].Target, switchEntity);

    // The shot binding is spent; the others are not.
    const std::span<const ResponseBindingDesc> set =
        ResponseDefinitionsOf(WorldState).Sets.Bindings(StateOf(switchEntity).Set);
    const int shot = FindResponseBinding(set, ResponseKeyHash("shot"));
    ASSERT_GE(shot, 0);
    EXPECT_EQ(StateOf(switchEntity).Consumed, 1u << shot);
    EXPECT_EQ(System->Deliver(switchEntity, TestStruck{ player, Charge, {} }), 0u);
}

TEST_F(ResponsesFixture, TwoOccurrencesBeforeThePassRunAOnceBindingOnce)
{
    const EntityId switchEntity =
        Place(Bindings({ Binding("shot", "test.struck", kRecordSelf, R"("once": true)") }));
    const EntityId a = WorldState.CreateEntity();
    const EntityId b = WorldState.CreateEntity();

    ASSERT_EQ(System->Deliver(switchEntity, TestStruck{ a, Charge, {} }), 1u);
    ASSERT_EQ(System->Deliver(switchEntity, TestStruck{ b, Charge, {} }), 1u);
    Tick();

    EXPECT_EQ(Calls.size(), 1u) << "two projectiles in one tick ran a once binding twice";
    EXPECT_EQ(System->Pending(), 0u);
    EXPECT_EQ(StateOf(switchEntity).Consumed, 1u);
}

// A once binding is spent by the first occurrence whose conditions hold, not
// by the first admitted: a default shot and a charge shot in one tick still
// open a charge-only switch.
TEST_F(ResponsesFixture, AFailingConditionLeavesAOnceBindingForTheNextOccurrence)
{
    const EntityId switchEntity = Place(Bindings({ Binding("shot", "test.struck", R"({
        "conditions": [{"tag": "attack", "tags": {"any": ["Test.Attack.Charge"]}}],
        "commands": [{"verb": "test.record", "arguments": {"target": {"port": "instigator"}}}]})",
                                                            R"("once": true)") }));
    const EntityId weak = WorldState.CreateEntity();
    const EntityId strong = WorldState.CreateEntity();

    ASSERT_EQ(System->Deliver(switchEntity, TestStruck{ weak, Default, {} }), 1u);
    ASSERT_EQ(System->Deliver(switchEntity, TestStruck{ strong, Strong, {} }), 1u);
    ASSERT_EQ(System->Deliver(switchEntity, TestStruck{ strong, Charge, {} }), 1u);
    Tick();

    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_EQ(Calls[0].Target, strong) << "a charged-strong shot is a charge shot, hierarchically";
}

TEST_F(ResponsesFixture, AnOccurrenceWaitingOutACooldownIsHeldBackWhenItRuns)
{
    const EntityId target = Place(Bindings({ Binding("hit", "test.struck", kRecordSelf, R"("cooldown": 0.5)") }));
    const EntityId shooter = WorldState.CreateEntity();

    ASSERT_EQ(System->Deliver(target, TestStruck{ shooter, Charge, {} }), 1u);
    ASSERT_EQ(System->Deliver(target, TestStruck{ shooter, Charge, {} }), 1u);
    Tick();
    EXPECT_EQ(Calls.size(), 1u) << "the second admitted occurrence ran inside the first's cooldown";
    EXPECT_EQ(System->Pending(), 0u);

    ASSERT_EQ(System->Deliver(target, TestStruck{ shooter, Charge, {} }), 0u) << "admitted while cooling";
    for (int i = 0; i < 5; ++i)
        Tick();
    ASSERT_EQ(System->Deliver(target, TestStruck{ shooter, Charge, {} }), 1u);
    Tick();
    EXPECT_EQ(Calls.size(), 2u);
}

TEST_F(ResponsesFixture, ATagConditionNeedsATagPortAndKnownTags)
{
    const EntityId target = Place(Bindings({
        Binding("wrong_port", "test.struck", R"({"conditions": [{"tag": "instigator", "tags": {"any": ["Test.Red"]}}]})"),
        Binding("no_tag", "test.struck", R"({"conditions": [{"tag": "attack", "tags": {"any": ["Test.Nothing"]}}]})"),
    }));
    ASSERT_EQ(System->Deliver(target, TestStruck{ target, Charge, {} }), 2u);
    Tick();

    EXPECT_EQ(System->Stats().Gated, 2u) << "a condition that did not compile held";
    const ResponseDefinitions& definitions = ResponseDefinitionsOf(WorldState);
    const std::span<const ResponseBindingDesc> set = definitions.Sets.Bindings(StateOf(target).Set);
    ASSERT_EQ(set.size(), 2u);
    const auto noTag = definitions.Sequences.Diagnostics(set[0].Sequence);
    const auto wrongPort = definitions.Sequences.Diagnostics(set[1].Sequence);
    ASSERT_EQ(noTag.size(), 1u);
    EXPECT_NE(noTag[0].find("Test.Nothing"), std::string::npos) << noTag[0];
    ASSERT_EQ(wrongPort.size(), 1u);
    EXPECT_NE(wrongPort[0].find("not a gameplay tag"), std::string::npos) << wrongPort[0];
}

// ---------------------------------------------------------------------------
// Lifetimes: what is captured, what is abandoned.
// ---------------------------------------------------------------------------

TEST_F(ResponsesFixture, ADestroyedRecipientStillRunsWhatItCaptured)
{
    const EntityId barrel = Place(Bindings({ Binding("detonated", "test.struck", R"({"commands": [
        {"verb": "test.record", "arguments": {"target": {"port": "instigator"}}},
        {"verb": "test.second", "arguments": {"target": {"port": "self"}}}]})",
                                                      R"("once": true)") }));
    const EntityId wall = WorldState.CreateEntity();

    // The producer delivers, then ends the entity in the same tick.
    ASSERT_EQ(System->Deliver(barrel, TestStruck{ wall, Charge, {} }), 1u);
    ASSERT_EQ(System->Deliver(barrel, TestStruck{ wall, Charge, {} }), 1u);
    WorldState.DestroyEntity(barrel);
    Tick();

    ASSERT_EQ(Calls.size(), 2u) << "a terminal response did not run, or ran twice";
    EXPECT_EQ(Calls[0].Target, wall) << "a command naming another entity still acts on it";
    EXPECT_EQ(Calls[1].Target, barrel) << "self is the captured, now stale, id";
    EXPECT_FALSE(WorldState.IsAlive(Calls[1].Target));
    EXPECT_EQ(System->Pending(), 0u);
}

TEST_F(ResponsesFixture, AResponseInADetachedZoneIsAbandonedNotRunAsTerminal)
{
    const StoragePartitionId zone{ 4 };
    Logic.Add(zone);
    const EntityId target = Place(Bindings({ Binding("hit", "test.struck", kRecordSelf) }), zone);
    const EntityId shooter = WorldState.CreateEntity();

    ASSERT_EQ(System->Deliver(target, TestStruck{ shooter, Charge, {} }), 1u);
    Residency(ZoneResidencyChangeKind::Detaching, zone);
    WorldState.DestroyEntity(target);
    Logic.Remove(zone);
    Tick();

    EXPECT_TRUE(Calls.empty()) << "a zone that streamed out ran its entity's responses";
}

TEST_F(ResponsesFixture, AReactionThatDeliversRunsItsChainOneLinkPerPass)
{
    // The first's response records itself, then strikes whoever struck it.
    const EntityId first = Place(Bindings({ Binding("hit", "test.struck", R"({"commands": [
        {"verb": "test.record", "arguments": {"target": {"port": "self"}}},
        {"verb": "test.strike", "arguments": {"target": {"port": "instigator"}}}]})") }));
    const EntityId second = Place(Bindings({ Binding("hit", "test.struck", kRecordSelf) }));

    ASSERT_EQ(System->Deliver(first, TestStruck{ second, Charge, {} }), 1u);
    Tick();
    ASSERT_EQ(Calls.size(), 1u) << "a reaction's reaction ran inside it";
    EXPECT_EQ(Calls[0].Target, first);
    EXPECT_EQ(System->Pending(), 1u);
    Tick();
    ASSERT_EQ(Calls.size(), 2u);
    EXPECT_EQ(Calls[1].Target, second);
}

TEST_F(ResponsesFixture, AFullQueueRefusesAndCountsAndKeepsWhatItAdmitted)
{
    System->SetCapacity(2);
    const EntityId target = Place(Bindings({ Binding("hit", "test.struck", kRecordSelf) }));
    const EntityId shooter = WorldState.CreateEntity();

    EXPECT_EQ(System->Deliver(target, TestStruck{ shooter, Charge, {} }), 1u);
    EXPECT_EQ(System->Deliver(target, TestStruck{ shooter, Charge, {} }), 1u);
    EXPECT_EQ(System->Deliver(target, TestStruck{ shooter, Charge, {} }), 0u);
    EXPECT_EQ(System->Stats().Refused, 1u);
    Tick();
    EXPECT_EQ(Calls.size(), 2u);
}

TEST_F(ResponsesFixture, APayloadItsDeclarationRefusesIsNotAdmitted)
{
    const EntityId target = Place(Bindings({ Binding("hit", "test.struck", kRecordSelf) }));
    EXPECT_EQ(System->Deliver(target, TestMisstruck{ target }), 0u);
    EXPECT_EQ(System->Stats().InvalidPayloads, 1u);
    EXPECT_EQ(System->Pending(), 0u);
}

// ---------------------------------------------------------------------------
// Binding identity, schemas and zone memory.
// ---------------------------------------------------------------------------

TEST_F(ResponsesFixture, ASetChangeKeepsStateByKeyAndResetsAChangedBinding)
{
    const std::string kept = Binding("kept", "test.struck", kRecordSelf, R"("once": true)");
    const EntityId target = Place(Bindings({ Binding("changed", "test.used", kRecordSelf), kept }));
    ASSERT_EQ(System->Deliver(target, TestStruck{ target, Charge, {} }), 1u);
    Tick();
    ASSERT_EQ(SetEnabled(target, "changed", false), VerbAdmission::Accepted);

    // A binding added ahead of both by key moves every position.
    ASSERT_TRUE(SetResponseBindings(WorldState, target,
                                    Json(Bindings({ Binding("added", "test.used", kRecordSelf),
                                                    Binding("changed", "test.used", kRecordInstigator), kept }))));
    const std::span<const ResponseBindingDesc> set =
        ResponseDefinitionsOf(WorldState).Sets.Bindings(StateOf(target).Set);
    const int keptAt = FindResponseBinding(set, ResponseKeyHash("kept"));
    const int changedAt = FindResponseBinding(set, ResponseKeyHash("changed"));
    ASSERT_EQ(keptAt, 2);
    EXPECT_EQ(StateOf(target).Consumed, 1u << keptAt) << "the consumed state did not follow its key";
    EXPECT_EQ(StateOf(target).Disabled & (1u << changedAt), 0u)
        << "a binding whose text changed kept the state of the old text";
}

TEST_F(ResponsesFixture, AChangedEventDeclarationAbandonsWhatWasCapturedAndRecompiles)
{
    const EntityId target = Place(Bindings({ Binding("hit", "test.struck", R"({"commands": [
        {"verb": "test.record", "arguments": {"target": {"port": "self"}, "amount": {"port": "distance"}}}]})") }));
    ASSERT_EQ(System->Deliver(target, TestStruck{ target, Charge, {} }), 1u);
    Tick();
    EXPECT_EQ(System->Stats().Failed, 1u) << "a port the event did not declare read as something";

    ASSERT_EQ(System->Deliver(target, TestStruck{ target, Charge, {} }), 1u);
    {
        AuthoredEventRegistrationScope events(*FindAuthoredEventRegistry(WorldState), "test");
        (void)events.Declare(StruckEvent(true));
        ASSERT_TRUE(events.Commit());
    }
    Tick();
    EXPECT_EQ(System->Stats().Abandoned, 1u) << "a payload captured under the old declaration ran";

    // What next reads the binding compiles it against the new payload.
    ResponseDefinitions& definitions = ResponseDefinitionsOf(WorldState);
    const VerbSequenceKey sequence = definitions.Sets.Bindings(StateOf(target).Set).front().Sequence;
    definitions.Sequences.Revalidate(WorldState);
    ASSERT_NE(definitions.Sequences.Resolve(sequence, WorldState, nullptr, nullptr), nullptr);
    EXPECT_TRUE(definitions.Sequences.Diagnostics(sequence).empty())
        << "the binding did not recompile against the new declaration";
}

TEST_F(ResponsesFixture, AnUndeclaredEventIsReportedAndItsBindingNeverMatches)
{
    const EntityId target = Place(Bindings({ Binding("hit", "test.nothing", kRecordSelf) }));
    ResponseDefinitions& definitions = ResponseDefinitionsOf(WorldState);
    (void)definitions.Sets.Resolve(StateOf(target).Set, WorldState);
    const std::span<const std::string> diagnostics = definitions.Sets.Diagnostics(StateOf(target).Set);
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_NE(diagnostics[0].find("test.nothing"), std::string::npos);
    EXPECT_EQ(System->Deliver(target, TestStruck{ target, Charge, {} }), 0u);
}

TEST_F(ResponsesFixture, AZoneRemembersConsumedAndDisabledBindingsAcrossUnloadAndReload)
{
    const StoragePartitionId zone{ 5 };
    Logic.Add(zone);
    const std::string bindings = Bindings({ Binding("open", "test.struck", kRecordSelf, R"("once": true)"),
                                            Binding("use", "test.used", kRecordSelf),
                                            Binding("idle", "test.used", kRecordInstigator) });
    const PersistentEntityId identity{ 0x1234 };
    const EntityId placed = Place(bindings, zone);
    WorldState.AddComponent<PersistentIdComponent>(placed, PersistentIdComponent{ identity });
    ASSERT_EQ(System->Deliver(placed, TestStruck{ placed, Charge, {} }), 1u);
    Tick();
    ASSERT_EQ(SetEnabled(placed, "use", false), VerbAdmission::Accepted);
    const Responses before = StateOf(placed);

    Residency(ZoneResidencyChangeKind::Detaching, zone);
    WorldState.DestroyEntity(placed);
    EXPECT_FALSE(ZoneState->ComponentState(ZoneId{ 1 }, identity, ResolveComponentTypeId<Responses>()).empty());

    // The zone comes back: the entity as authored, then its remembered state.
    const EntityId reloaded = Place(bindings, zone);
    WorldState.AddComponent<PersistentIdComponent>(reloaded, PersistentIdComponent{ identity });
    EXPECT_EQ(StateOf(reloaded).Consumed, 0u);
    Residency(ZoneResidencyChangeKind::Attached, zone);
    EXPECT_EQ(StateOf(reloaded).Consumed, before.Consumed);
    EXPECT_EQ(StateOf(reloaded).Disabled, before.Disabled);

    // Back as authored, the zone forgets it.
    *WorldState.TryGet<Responses>(reloaded) = Responses{ StateOf(reloaded).Set, 0, 0 };
    Residency(ZoneResidencyChangeKind::Detaching, zone);
    EXPECT_TRUE(ZoneState->ComponentState(ZoneId{ 1 }, identity, ResolveComponentTypeId<Responses>()).empty());
}

// ---------------------------------------------------------------------------
// One mechanism: a volume's sensor responses and an entity's behaviors.
// ---------------------------------------------------------------------------

TEST_F(ResponsesFixture, ASensorResponseAndANativeOccurrenceRunThroughTheSamePass)
{
    const EntityId entity = Place(Bindings({ Binding("walk", kSensorEnterEvent, kRecordActivator),
                                             Binding("hit", "test.struck", kRecordInstigator) }));
    const EntityId walker = WorldState.CreateEntity();
    const EntityId shooter = WorldState.CreateEntity();

    ASSERT_EQ(System->Deliver(entity, TestStruck{ shooter, Charge, {} }), 1u);
    Step({ { entity, walker } });

    ASSERT_EQ(Calls.size(), 2u);
    EXPECT_EQ(Calls[0].Target, shooter) << "the earlier admission ran first";
    EXPECT_EQ(Calls[1].Target, walker);
    EXPECT_EQ(System->Stats().Activations, 2u);
}
