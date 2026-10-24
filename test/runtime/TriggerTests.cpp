#include <gtest/gtest.h>

#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleRegistry.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/Trigger.h>
#include <logic/TriggerSequences.h>
#include <logic/TriggerSystem.h>
#include <logic/TriggerVerbs.h>
#include <physics/SensorOverlaps.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/ComponentRegistrar.h>
#include <world/SimulationAuthority.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A trigger turning sensor samples into its sequence's commands, at the fixed
// pass after the sample. Samples are published straight into SensorOverlaps,
// as the physics step would, so each policy is seen without a body in the way.

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

JsonValue Json(std::string_view text)
{
    std::optional<JsonValue> parsed = JsonParse(text);
    EXPECT_TRUE(parsed.has_value()) << text;
    return parsed.value_or(JsonValue());
}

constexpr std::string_view kRecordActivator = R"({"commands": [
    {"verb": "test.record", "arguments": {"target": {"port": "activator"}}}]})";

class TriggerFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ComponentRegistrar registrar(WorldState);
        registrar.Add<Trigger>();
        registrar.Add<GameplayTagContainer>();
        registrar.Add<LocalTransform>();
        registrar.Add<WorldTransform>();

        Tags = &WorldState.AddResource<GameplayTagRegistry>();
        Red = *Tags->RegisterTag("Test.Red");

        InstallAuthoredVocabulary(WorldState);
        Verbs = FindVerbRegistry(WorldState);
        {
            VerbRegistrationScope scope(*Verbs, "test");
            for (const char* name : { "test.record", "test.second", "test.refuse" })
            {
                VerbDefinition verb;
                verb.Name = name;
                verb.Arguments = RecordArguments();
                (void)scope.Declare(std::move(verb));
            }
            DeclareTriggerVerbs(scope);
            ASSERT_TRUE(scope.Commit());
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
        Operations = std::make_unique<TriggerOperations>(WorldState);
        RedAnswer = std::make_unique<RedQuery>(WorldState, Red);
        Tokens.push_back(Dispatcher->Bind(Verbs->Find("test.record"), *Record));
        Tokens.push_back(Dispatcher->Bind(Verbs->Find("test.second"), *Second));
        Tokens.push_back(Dispatcher->Bind(Verbs->Find("test.refuse"), *Refuse));
        Tokens.push_back(Dispatcher->Bind<&InvokeTriggerEnable>(Verbs->Find(kTriggerEnableVerb), *Operations));
        Tokens.push_back(Dispatcher->Bind<&InvokeTriggerInteract>(Verbs->Find(kTriggerInteractVerb), *Operations));
        QueryToken = Queries->Bind<&RedQuery::Evaluate>(FindAuthoredQueryRegistry(WorldState)->Find("test.is_red"),
                                                       *RedAnswer);
        ReachToken = Queries->Bind<&ReachQuery::Evaluate>(FindAuthoredQueryRegistry(WorldState)->Find("test.reach"),
                                                         Reach);

        Overlaps = &WorldState.AddResource<SensorOverlaps>();
        Logic.Add(StoragePartitionId::Default());
        System = &RegisterTriggerSystem(Schedule, WorldState, *Dispatcher, Queries.get(), nullptr, nullptr, Console,
                                        Logging);
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

    EntityId PlaceTrigger(std::string_view sequence, const std::function<void(Trigger&)>& configure = {},
                          StoragePartitionId partition = StoragePartitionId::Default())
    {
        const EntityId entity = WorldState.CreateEntity(partition);
        Trigger trigger;
        if (configure)
            configure(trigger);
        WorldState.AddComponent<Trigger>(entity, trigger);
        EXPECT_TRUE(SetTriggerSequence(WorldState, entity, Json(sequence)));
        return entity;
    }

    // The physics step's sample at the current tick: these pairs are occupied.
    void Sample(std::initializer_list<SensorPair> occupied)
    {
        Overlaps->Begin();
        for (const SensorPair& pair : occupied)
            EXPECT_TRUE(Overlaps->Observe(pair, true));
        EXPECT_TRUE(Overlaps->Commit(TickIndex));
    }

    void Tick()
    {
        ++TickIndex;
        FixedLogicContext fixed{
            .Config = Config,
            .Runtime = Runtime,
            .Time = FixedSimTime{ .DeltaSeconds = 0.1, .TickIndex = TickIndex },
            .Entities = WorldState,
            .Partitions = Logic,
        };
        Schedule.RunFixedLogic(fixed);
    }

    // A sample, then the pass that reads it.
    void Step(std::initializer_list<SensorPair> occupied)
    {
        Sample(occupied);
        Tick();
    }

    LoggingProvider Logging;
    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    ConsoleRegistry Console;
    World WorldState;
    StoragePartitionSet Logic;
    EngineSchedule Schedule;
    GameplayTagRegistry* Tags = nullptr;
    GameplayTagId Red;
    VerbRegistry* Verbs = nullptr;
    std::unique_ptr<VerbDispatcher> Dispatcher;
    std::unique_ptr<AuthoredQueryDispatcher> Queries;
    std::vector<RecordedCall> Calls;
    std::unique_ptr<RecordingOperation> Record;
    std::unique_ptr<RecordingOperation> Second;
    std::unique_ptr<RecordingOperation> Refuse;
    std::unique_ptr<TriggerOperations> Operations;
    std::unique_ptr<RedQuery> RedAnswer;
    std::vector<VerbBindingToken> Tokens;
    AuthoredQueryBindingToken QueryToken;
    ReachQuery Reach;
    AuthoredQueryBindingToken ReachToken;
    SensorOverlaps* Overlaps = nullptr;
    TriggerSystem* System = nullptr;
    std::uint64_t TickIndex = 0;
};
} // namespace

TEST_F(TriggerFixture, EnterRunsTheSequenceAtThePassAfterTheSampleWithProvenance)
{
    const EntityId trigger = PlaceTrigger(kRecordActivator);
    const EntityId activator = WorldState.CreateEntity();

    Sample({ { trigger, activator } });
    EXPECT_TRUE(Calls.empty()) << "a sequence ran inside the physics sample";
    Tick();
    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_EQ(Calls[0].Target, activator);
    EXPECT_EQ(Calls[0].Producer, trigger);
    EXPECT_EQ(Calls[0].Instigator, activator);
    EXPECT_EQ(Calls[0].Tick, TickIndex);

    // Staying inside is not entering again.
    Step({ { trigger, activator } });
    Step({ { trigger, activator } });
    EXPECT_EQ(Calls.size(), 1u);
}

TEST_F(TriggerFixture, AFastCrossingStillEntersAndExits)
{
    const EntityId enter = PlaceTrigger(kRecordActivator);
    const EntityId exit = PlaceTrigger(kRecordActivator, [](Trigger& t) { t.Activation = TriggerActivation::Exit; });
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

TEST_F(TriggerFixture, ExitRunsWhenTheActivatorLeaves)
{
    const EntityId trigger = PlaceTrigger(kRecordActivator, [](Trigger& t) { t.Activation = TriggerActivation::Exit; });
    const EntityId activator = WorldState.CreateEntity();

    Step({ { trigger, activator } });
    Step({ { trigger, activator } });
    EXPECT_TRUE(Calls.empty());
    Step({});
    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_EQ(Calls[0].Target, activator);
}

TEST_F(TriggerFixture, StayRunsOnEntryThenEveryInterval)
{
    // Ticks are a tenth of a second: every 0.3 s is every third sample.
    const EntityId trigger = PlaceTrigger(kRecordActivator, [](Trigger& t) {
        t.Activation = TriggerActivation::Stay;
        t.StayIntervalSeconds = 0.3f;
    });
    const EntityId activator = WorldState.CreateEntity();

    for (int i = 0; i < 7; ++i)
        Step({ { trigger, activator } });
    EXPECT_EQ(Calls.size(), 3u) << "entry, then two intervals";

    // Leaving and coming back starts over on entry.
    Step({});
    Step({ { trigger, activator } });
    EXPECT_EQ(Calls.size(), 4u);
}

TEST_F(TriggerFixture, ManyActivatorsRunInIdentityOrderAndCommandsInAuthoredOrder)
{
    const EntityId trigger = PlaceTrigger(R"({"commands": [
        {"verb": "test.record", "arguments": {"target": {"port": "activator"}}},
        {"verb": "test.second", "arguments": {"target": {"port": "activator"}}}]})");
    const EntityId first = WorldState.CreateEntity();
    const EntityId second = WorldState.CreateEntity();

    Step({ { trigger, second }, { trigger, first } });

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

TEST_F(TriggerFixture, AFailedCommandContinuesOrStopsAsItsPolicySays)
{
    const EntityId continues = PlaceTrigger(R"({"commands": [
        {"verb": "test.refuse", "arguments": {"target": {"port": "activator"}}},
        {"verb": "test.record", "arguments": {"target": {"port": "activator"}}}]})");
    const EntityId stops = PlaceTrigger(R"({"commands": [
        {"verb": "test.refuse", "arguments": {"target": {"port": "activator"}}, "on_failure": "stop"},
        {"verb": "test.record", "arguments": {"target": {"port": "activator"}}}]})");
    const EntityId activator = WorldState.CreateEntity();

    Step({ { continues, activator } });
    ASSERT_EQ(Calls.size(), 2u);
    EXPECT_EQ(Calls[1].Verb, "test.record");

    Calls.clear();
    Step({ { continues, activator }, { stops, activator } });
    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_EQ(Calls[0].Verb, "test.refuse");
    EXPECT_EQ(System->Stats().Failed, 2u);
}

TEST_F(TriggerFixture, AnUnknownVerbFailsItsCommandAndTheRestStillRun)
{
    const EntityId trigger = PlaceTrigger(R"({"commands": [
        {"verb": "test.missing", "arguments": {"target": {"port": "activator"}}},
        {"verb": "test.record", "arguments": {"target": {"port": "nowhere"}}},
        {"verb": "test.record", "arguments": {"target": {"port": "activator"}, "amount": 2}}]})");
    const EntityId activator = WorldState.CreateEntity();

    Step({ { trigger, activator } });

    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_DOUBLE_EQ(Calls[0].Amount, 2.0);
    EXPECT_EQ(System->Stats().Failed, 2u);
    const VerbSequenceStore& store = TriggerSequencesOf(WorldState);
    const std::span<const std::string> diagnostics =
        store.Diagnostics(WorldState.TryGet<Trigger>(trigger)->Sequence);
    ASSERT_EQ(diagnostics.size(), 2u);
    EXPECT_NE(diagnostics[0].find("test.missing"), std::string::npos);
    EXPECT_NE(diagnostics[1].find("nowhere"), std::string::npos);
}

TEST_F(TriggerFixture, OnceEndsTheTriggerAfterItRuns)
{
    const EntityId trigger = PlaceTrigger(kRecordActivator, [](Trigger& t) { t.Once = true; });
    const EntityId first = WorldState.CreateEntity();
    const EntityId second = WorldState.CreateEntity();

    Step({ { trigger, first }, { trigger, second } });

    EXPECT_EQ(Calls.size(), 1u) << "a once trigger ran for a second activator in its own pass";
    EXPECT_FALSE(WorldState.IsAlive(trigger));
    EXPECT_EQ(TriggerSequencesOf(WorldState).Size(), 0u) << "the consumed trigger's sequence was kept";
}

TEST_F(TriggerFixture, ATriggerCooldownHoldsEveryoneAndAnActivatorCooldownOnlyThatOne)
{
    const auto entering = [](float cooldown, TriggerCooldownScope scope) {
        return [=](Trigger& t) {
            t.CooldownSeconds = cooldown;
            t.CooldownScope = scope;
        };
    };
    const EntityId shared = PlaceTrigger(kRecordActivator, entering(0.5f, TriggerCooldownScope::Trigger));
    const EntityId own = PlaceTrigger(kRecordActivator, entering(0.5f, TriggerCooldownScope::Activator));
    const EntityId a = WorldState.CreateEntity();
    const EntityId b = WorldState.CreateEntity();

    Step({ { shared, a }, { own, a } });
    ASSERT_EQ(Calls.size(), 2u);
    Calls.clear();

    // B arrives while A's activation cools: only the per-activator trigger runs.
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

TEST_F(TriggerFixture, ADisabledTriggerWaitsForEnable)
{
    const EntityId trigger = PlaceTrigger(kRecordActivator, [](Trigger& t) { t.Enabled = false; });
    const EntityId activator = WorldState.CreateEntity();

    Step({ { trigger, activator } });
    EXPECT_TRUE(Calls.empty());

    VerbInvocation request;
    AuthoredArguments arguments(1);
    arguments.Set(0, AuthoredValue::Entity(trigger));
    request.Arguments = &arguments;
    EXPECT_EQ(Operations->Enable(request), VerbAdmission::Accepted);
    EXPECT_TRUE(WorldState.TryGet<Trigger>(trigger)->Enabled);

    Step({ { trigger, activator } });
    EXPECT_TRUE(Calls.empty()) << "enabling ran for an activator already inside";
    Step({});
    Step({ { trigger, activator } });
    EXPECT_EQ(Calls.size(), 1u);
}

TEST_F(TriggerFixture, ConditionsFilterTheActivatorByComponentTagAndQuery)
{
    const EntityId byTag = PlaceTrigger(R"({
        "conditions": [{"entity": "activator", "has": ["sencha.gameplay_tag_container"], "tags": {"all": ["Test.Red"]}}],
        "commands": [{"verb": "test.record", "arguments": {"target": {"port": "activator"}}}]})");
    const EntityId byQuery = PlaceTrigger(R"({
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

TEST_F(TriggerFixture, AValueMayBeGivenInTheTriggersFrameAndScaled)
{
    const EntityId trigger = PlaceTrigger(R"({"commands": [
        {"verb": "test.record", "arguments": {
            "target": {"port": "activator"},
            "amount": {"const": 2, "scale": 3},
            "direction": {"const": [0, 0, 1], "frame": "trigger", "scale": 2}}}]})");
    // Turned a quarter about up: its forward (+Z) is world +X.
    WorldTransform pose;
    pose.Value.Rotation = Quatf::FromAxisAngle(Vec3d(0.0f, 1.0f, 0.0f), 1.57079633f);
    WorldState.AddComponent<WorldTransform>(trigger, pose);
    const EntityId activator = WorldState.CreateEntity();

    Step({ { trigger, activator } });

    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_DOUBLE_EQ(Calls[0].Amount, 6.0);
    EXPECT_NEAR(Calls[0].Direction.Components[0], 2.0, 1e-5);
    EXPECT_NEAR(Calls[0].Direction.Components[1], 0.0, 1e-5);
    EXPECT_NEAR(Calls[0].Direction.Components[2], 0.0, 1e-5);
}

TEST_F(TriggerFixture, AnActivatorThatEndsBeforeThePassIsNotRunFor)
{
    const EntityId trigger = PlaceTrigger(kRecordActivator);
    const EntityId activator = WorldState.CreateEntity();

    Sample({ { trigger, activator } });
    WorldState.DestroyEntity(activator);
    const EntityId reused = WorldState.CreateEntity();
    Tick();

    EXPECT_TRUE(Calls.empty());
    EXPECT_EQ(System->Stats().Abandoned, 1u);
    (void)reused;
}

TEST_F(TriggerFixture, ADormantZonesActivationsAreDroppedNotRunLater)
{
    const StoragePartitionId zone{ 7 };
    const EntityId trigger = PlaceTrigger(kRecordActivator, {}, zone);
    const EntityId activator = WorldState.CreateEntity();

    // The zone does not participate in logic this pass.
    Step({ { trigger, activator } });
    EXPECT_TRUE(Calls.empty());
    EXPECT_EQ(System->Pending(), 0u) << "an activation waited for a dormant zone";

    Logic.Add(zone);
    Step({ { trigger, activator } });
    EXPECT_TRUE(Calls.empty()) << "a stale entry ran when its zone woke";
}

TEST_F(TriggerFixture, AClientRunsNothing)
{
    WorldState.AddResource<SimulationAuthority>().Authoritative = false;
    const EntityId trigger = PlaceTrigger(kRecordActivator);
    const EntityId activator = WorldState.CreateEntity();

    Step({ { trigger, activator } });
    EXPECT_TRUE(Calls.empty());
    EXPECT_EQ(System->Pending(), 0u);

    // Becoming the authority later does not replay what the client saw.
    WorldState.GetResource<SimulationAuthority>().Authoritative = true;
    Step({ { trigger, activator } });
    EXPECT_TRUE(Calls.empty());
}

TEST_F(TriggerFixture, AnInteractionRunsTheTriggersTheActivatorStandsIn)
{
    const EntityId inside = PlaceTrigger(kRecordActivator, [](Trigger& t) { t.Activation = TriggerActivation::Interaction; });
    const EntityId elsewhere =
        PlaceTrigger(kRecordActivator, [](Trigger& t) { t.Activation = TriggerActivation::Interaction; });
    const EntityId activator = WorldState.CreateEntity();
    const EntityId other = WorldState.CreateEntity();

    Step({ { inside, activator }, { elsewhere, other } });
    EXPECT_TRUE(Calls.empty()) << "standing in an interaction trigger activated it";

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

TEST_F(TriggerFixture, ActivationsPastTheBudgetWaitForTheNextPassInOrder)
{
    System->SetCommandBudget(2);
    const EntityId trigger = PlaceTrigger(kRecordActivator);
    std::vector<EntityId> activators;
    for (int i = 0; i < 5; ++i)
        activators.push_back(WorldState.CreateEntity());

    Overlaps->Begin();
    for (const EntityId activator : activators)
        ASSERT_TRUE(Overlaps->Observe({ trigger, activator }, true));
    ASSERT_TRUE(Overlaps->Commit(TickIndex));
    Tick();
    EXPECT_EQ(Calls.size(), 2u);
    EXPECT_EQ(System->Pending(), 3u);
    Tick();
    Tick();
    ASSERT_EQ(Calls.size(), 5u);
    for (std::size_t i = 0; i < activators.size(); ++i)
        EXPECT_EQ(Calls[i].Target, activators[i]);
}

TEST_F(TriggerFixture, EntitiesAuthoredAlikeShareOneSequence)
{
    const EntityId a = PlaceTrigger(kRecordActivator);
    const EntityId b = PlaceTrigger(kRecordActivator);
    EXPECT_EQ(WorldState.TryGet<Trigger>(a)->Sequence, WorldState.TryGet<Trigger>(b)->Sequence);
    EXPECT_EQ(TriggerSequencesOf(WorldState).Size(), 1u);

    WorldState.DestroyEntity(a);
    EXPECT_EQ(TriggerSequencesOf(WorldState).Size(), 1u);
    ASSERT_TRUE(SetTriggerSequence(WorldState, b, EmptyVerbSequence()));
    EXPECT_EQ(TriggerSequencesOf(WorldState).Size(), 1u) << "the replaced sequence was kept";
    WorldState.DestroyEntity(b);
    EXPECT_EQ(TriggerSequencesOf(WorldState).Size(), 0u);
}

TEST_F(TriggerFixture, AValueMayBeAQuerysAnswerOrOneMemberOfIt)
{
    const EntityId trigger = PlaceTrigger(R"({"commands": [
        {"verb": "test.record", "arguments": {
            "target": {"port": "activator"},
            "amount": {"query": "test.reach", "member": "distance", "scale": 2}}}]})");
    const EntityId activator = WorldState.CreateEntity();

    Step({ { trigger, activator } });

    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_DOUBLE_EQ(Calls[0].Amount, 9.0);
    EXPECT_TRUE(TriggerSequencesOf(WorldState).Diagnostics(WorldState.TryGet<Trigger>(trigger)->Sequence).empty());
}

TEST_F(TriggerFixture, AnAnswerOfTheWrongKindIsRefusedWhenItCompiles)
{
    const EntityId trigger = PlaceTrigger(R"({"commands": [
        {"verb": "test.record", "arguments": {
            "target": {"port": "activator"},
            "amount": {"query": "test.reach", "member": "clear"}}}]})");
    const EntityId activator = WorldState.CreateEntity();

    Step({ { trigger, activator } });

    EXPECT_TRUE(Calls.empty());
    EXPECT_EQ(System->Stats().Failed, 1u);
    const auto diagnostics = TriggerSequencesOf(WorldState).Diagnostics(WorldState.TryGet<Trigger>(trigger)->Sequence);
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_NE(diagnostics[0].find("different kind"), std::string::npos) << diagnostics[0];
}

TEST_F(TriggerFixture, AZoneThatUnloadsTakesItsWaitingActivationsWithIt)
{
    System->SetCommandBudget(1);
    const StoragePartitionId zone{ 3 };
    Logic.Add(zone);
    const EntityId first = PlaceTrigger(kRecordActivator, {}, zone);
    const EntityId second = PlaceTrigger(kRecordActivator, {}, zone);
    const EntityId activator = WorldState.CreateEntity();

    Step({ { first, activator }, { second, activator } });
    ASSERT_EQ(Calls.size(), 1u);
    ASSERT_EQ(System->Pending(), 1u);

    ZoneResidencyChange detaching;
    detaching.Kind = ZoneResidencyChangeKind::Detaching;
    detaching.Partition = zone;
    ZoneResidencyContext residency{ .Config = Config, .Entities = WorldState, .Changes = { &detaching, 1 } };
    Schedule.RunZoneResidency(residency);
    EXPECT_EQ(System->Pending(), 0u);
    Tick();
    EXPECT_EQ(Calls.size(), 1u) << "an activation ran for a zone that had gone";
}
