#include <gtest/gtest.h>

#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <assets/data/DataAssetCache.h>
#include <authored/VerbBindingData.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <core/assets/AssetRegistry.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/CommandBuffer.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <logic/VerbRelay.h>
#include <logic/VerbRelayBindingStore.h>
#include <logic/VerbRelaySystem.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/ComponentRegistrar.h>

#include <memory>
#include <string>
#include <vector>

// A placed relay firing an authored binding at the fixed-logic drain.
//
// Driven the way the loop drives it -- a schedule, a fixed tick, the logic
// partition set -- against a real data-asset cache holding a real compiled
// binding set, with nothing but the World in between. What is being proved is
// timing and lifetime: a request is one-shot, acts in admission order at the
// next drain, and never waits for a relay that has gone.

namespace
{
constexpr std::string_view kLibraryPath = "asset://data/test/relays.sdata";
constexpr std::string_view kBindingKey = "relay.score";

// A verb with one entity argument and one integer, the shape the relay proof
// is written for.
[[nodiscard]] DataFieldSchema ScoreArguments()
{
    DataFieldSchema target;
    target.Key = "Target";
    target.Kind = DataFieldKind::Entity;
    DataFieldSchema amount;
    amount.Key = "Amount";
    amount.Kind = DataFieldKind::Int;
    DataFieldSchema root = EmptyVerbArguments();
    root.Children.push_back(std::move(target));
    root.Children.push_back(std::move(amount));
    return root;
}

[[nodiscard]] std::shared_ptr<VerbBindingLibrary> MakeLibrary(std::int64_t amount)
{
    auto library = std::make_shared<VerbBindingLibrary>();
    VerbBindingDesc desc;
    desc.Key = std::string(kBindingKey);
    desc.KeyId = MakeVerbBindingKey(kBindingKey);
    desc.VerbName = "test.score";
    desc.Inputs = { "target" };
    VerbBindingArgument target;
    target.Key = "Target";
    target.Source = VerbArgumentSource::Input;
    target.Text = "target";
    VerbBindingArgument constant;
    constant.Key = "Amount";
    constant.Source = VerbArgumentSource::Literal;
    constant.Literal = JsonValue(static_cast<double>(amount));
    desc.Arguments = { std::move(target), std::move(constant) };
    library->Bindings.push_back(std::move(desc));
    return library;
}

// Records what it was handed, in order, and can be told to activate a relay
// from inside its own call -- which is how "admitted during the drain waits
// for the next one" is exercised.
class ScoreOperation
{
public:
    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        Call call;
        call.Id = invocation.Id;
        call.Producer = invocation.Producer;
        (void)invocation.Arguments->TryGetEntity(0, call.Target);
        (void)invocation.Arguments->TryGetInt(1, call.Amount);
        Calls.push_back(call);
        if (Relay != nullptr && ReactivateDuringCall.IsValid())
        {
            const VerbValue self = VerbValue::Entity(ReactivateDuringCall);
            Nested = Relay->Activate(ReactivateDuringCall, { &self, 1 }, invocation.Id);
            ReactivateDuringCall = {};
        }
        return VerbAdmission::Accepted;
    }

    struct Call
    {
        InvocationId Id;
        EntityId Producer;
        EntityId Target;
        std::int64_t Amount = 0;
    };
    std::vector<Call> Calls;
    VerbRelaySystem* Relay = nullptr;
    EntityId ReactivateDuringCall;
    VerbAdmission Nested = VerbAdmission::Unavailable;
};

class VerbRelayFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ComponentRegistrar registrar(WorldState);
        registrar.Add<VerbRelay>();

        Verbs = &InstallVerbRegistry(WorldState);
        {
            VerbRegistrationScope scope(*Verbs, "test");
            VerbDefinition score;
            score.Name = "test.score";
            score.Arguments = ScoreArguments();
            (void)scope.Declare(std::move(score));
            ASSERT_TRUE(scope.Commit());
        }

        Library = Cache.Register(kLibraryPath, std::string(kVerbBindingsTypeName),
                                 MakeLibrary(5));
        ASSERT_TRUE(Library.IsValid());

        Dispatcher = std::make_unique<VerbDispatcher>(*Verbs);
        Dispatcher->SetTrace(&Trace);
        Token = Dispatcher->Bind(Verbs->Find("test.score"), Operation);

        Logic.Add(StoragePartitionId::Default());
        Relay = &RegisterVerbRelaySystem(Schedule, WorldState, *Dispatcher, Assets, Cache,
                                         Console, Logging);
        Operation.Relay = Relay;
        Schedule.Init();
    }

    void TearDown() override
    {
        Schedule.Shutdown();
        Token.Reset();
        Dispatcher.reset();
    }

    [[nodiscard]] EntityId PlaceRelay(StoragePartitionId partition = StoragePartitionId::Default())
    {
        const EntityId entity = WorldState.CreateEntity(partition);
        VerbRelay relay;
        relay.Bindings = Library;
        relay.Binding = MakeVerbBindingKey(kBindingKey);
        WorldState.AddComponent<VerbRelay>(entity, relay);
        return entity;
    }

    [[nodiscard]] VerbAdmission Activate(EntityId relay, EntityId target)
    {
        const VerbValue value = VerbValue::Entity(target);
        return Relay->Activate(relay, { &value, 1 });
    }

    void Tick()
    {
        FixedLogicContext fixed{
            .Config = Config,
            .Runtime = Runtime,
            .Time = {},
            .Entities = WorldState,
            .Partitions = Logic,
        };
        Schedule.RunFixedLogic(fixed);
    }

    LoggingProvider Logging;
    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    ConsoleRegistry Console;
    AssetRegistry Assets{ Logging };
    DataAssetCache Cache;
    World WorldState;
    StoragePartitionSet Logic;
    EngineSchedule Schedule;
    VerbRegistry* Verbs = nullptr;
    DataAssetHandle Library;
    std::unique_ptr<VerbDispatcher> Dispatcher;
    VerbTraceRing Trace{ 32 };
    ScoreOperation Operation;
    VerbBindingToken Token;
    VerbRelaySystem* Relay = nullptr;
};
}

TEST_F(VerbRelayFixture, AnActivationFiresAtTheNextDrainWithItsTypedInput)
{
    const EntityId relay = PlaceRelay();
    const EntityId target = WorldState.CreateEntity();

    EXPECT_EQ(Activate(relay, target), VerbAdmission::Accepted);
    EXPECT_EQ(Relay->Pending(), 1u);
    EXPECT_TRUE(Operation.Calls.empty()) << "a relay acted on the frame clock";

    Tick();
    ASSERT_EQ(Operation.Calls.size(), 1u);
    EXPECT_EQ(Operation.Calls[0].Target, target);
    EXPECT_EQ(Operation.Calls[0].Amount, 5);
    EXPECT_EQ(Operation.Calls[0].Producer, relay);
    EXPECT_TRUE(Operation.Calls[0].Id.IsValid());
    EXPECT_EQ(Relay->Pending(), 0u);

    // One-shot: a second tick runs nothing.
    Tick();
    EXPECT_EQ(Operation.Calls.size(), 1u);
}

TEST_F(VerbRelayFixture, ZeroOneAndManyRelaysDrainInAdmissionOrder)
{
    Tick();
    EXPECT_TRUE(Operation.Calls.empty());

    const EntityId first = PlaceRelay();
    const EntityId second = PlaceRelay();
    const EntityId third = PlaceRelay();
    EXPECT_EQ(Activate(third, third), VerbAdmission::Accepted);
    EXPECT_EQ(Activate(first, first), VerbAdmission::Accepted);
    EXPECT_EQ(Activate(second, second), VerbAdmission::Accepted);
    EXPECT_EQ(Activate(first, first), VerbAdmission::Accepted);

    Tick();
    ASSERT_EQ(Operation.Calls.size(), 4u);
    EXPECT_EQ(Operation.Calls[0].Producer, third);
    EXPECT_EQ(Operation.Calls[1].Producer, first);
    EXPECT_EQ(Operation.Calls[2].Producer, second);
    EXPECT_EQ(Operation.Calls[3].Producer, first);
    EXPECT_LT(Operation.Calls[0].Id.Value, Operation.Calls[3].Id.Value);
}

TEST_F(VerbRelayFixture, ARequestAdmittedDuringTheDrainWaitsForTheNextTick)
{
    const EntityId relay = PlaceRelay();
    Operation.ReactivateDuringCall = relay;

    ASSERT_EQ(Activate(relay, relay), VerbAdmission::Accepted);
    Tick();

    // The nested activation was admitted -- the queue is open during a drain --
    // and did not run in the same tick: no recursive drain, no unbounded chain.
    EXPECT_EQ(Operation.Nested, VerbAdmission::Accepted);
    EXPECT_EQ(Operation.Calls.size(), 1u);
    EXPECT_EQ(Relay->Pending(), 1u);

    Tick();
    ASSERT_EQ(Operation.Calls.size(), 2u);
    EXPECT_EQ(Relay->Pending(), 0u);
    // And the second carries the first as its parent.
    bool parented = false;
    for (const VerbTraceRecord& record : Trace.Snapshot())
        parented = parented || (record.Id == Operation.Calls[1].Id
                                && record.Parent == Operation.Calls[0].Id);
    EXPECT_TRUE(parented);
}

TEST_F(VerbRelayFixture, ADeadRelayIsAbandonedRatherThanWaitedFor)
{
    const EntityId relay = PlaceRelay();
    ASSERT_EQ(Activate(relay, relay), VerbAdmission::Accepted);
    WorldState.DestroyEntity(relay);

    Tick();
    EXPECT_TRUE(Operation.Calls.empty());
    EXPECT_EQ(Relay->Stats().Abandoned, 1u);
    EXPECT_EQ(Relay->Pending(), 0u);

    // A relay that lost its component is the same case.
    const EntityId stripped = PlaceRelay();
    ASSERT_EQ(Activate(stripped, stripped), VerbAdmission::Accepted);
    WorldState.RemoveComponent<VerbRelay>(stripped);
    Tick();
    EXPECT_TRUE(Operation.Calls.empty());
    EXPECT_EQ(Relay->Stats().Abandoned, 2u);

    bool traced = false;
    for (const VerbTraceRecord& record : Trace.Snapshot())
        traced = traced || (record.Event == VerbTraceEvent::Abandoned && record.Producer == relay);
    EXPECT_TRUE(traced);
}

TEST_F(VerbRelayFixture, ARelayInADormantPartitionNeitherFiresNorWaits)
{
    const StoragePartitionId dormant{ 7 };
    const EntityId relay = PlaceRelay(dormant);
    ASSERT_EQ(Activate(relay, relay), VerbAdmission::Accepted);

    // Not in this tick's logic set.
    Tick();
    EXPECT_TRUE(Operation.Calls.empty());
    EXPECT_EQ(Relay->Pending(), 0u);
    EXPECT_EQ(Relay->Stats().Abandoned, 1u);

    // Waking the zone later does not replay what was abandoned.
    Logic.Add(dormant);
    Tick();
    EXPECT_TRUE(Operation.Calls.empty());

    // But a fresh activation against the now-participating zone fires.
    ASSERT_EQ(Activate(relay, relay), VerbAdmission::Accepted);
    Tick();
    EXPECT_EQ(Operation.Calls.size(), 1u);
}

TEST_F(VerbRelayFixture, TheQueueIsBoundedAndOverflowIsRefusedVisibly)
{
    const EntityId relay = PlaceRelay();
    Relay->SetCapacity(2);
    EXPECT_EQ(Activate(relay, relay), VerbAdmission::Accepted);
    EXPECT_EQ(Activate(relay, relay), VerbAdmission::Accepted);
    EXPECT_EQ(Activate(relay, relay), VerbAdmission::QueueFull);
    EXPECT_EQ(Relay->Stats().Refused, 1u);

    Tick();
    EXPECT_EQ(Operation.Calls.size(), 2u) << "an accepted request was dropped";

    // The cvar is the knob, and it reaches the running system.
    EXPECT_TRUE(Console.SetCVar("logic.relay.queue_capacity", std::int64_t{ 3 },
                                { .Description = "test" }, ConsolePhase::EngineReady, true)
                    .Succeeded());
    EXPECT_EQ(Relay->Capacity(), 3u);
    EXPECT_EQ(Activate(relay, relay), VerbAdmission::Accepted);
    EXPECT_EQ(Activate(relay, relay), VerbAdmission::Accepted);
    EXPECT_EQ(Activate(relay, relay), VerbAdmission::Accepted);
    EXPECT_EQ(Activate(relay, relay), VerbAdmission::QueueFull);
}

TEST_F(VerbRelayFixture, ABindingAssetReloadTakesEffectAtTheNextActivation)
{
    const EntityId relay = PlaceRelay();
    ASSERT_EQ(Activate(relay, relay), VerbAdmission::Accepted);
    Tick();
    ASSERT_EQ(Operation.Calls.size(), 1u);
    EXPECT_EQ(Operation.Calls[0].Amount, 5);

    ASSERT_TRUE(Cache.ReloadInPlace(kLibraryPath, kVerbBindingsTypeName, MakeLibrary(9)));
    ASSERT_EQ(Activate(relay, relay), VerbAdmission::Accepted);
    Tick();
    ASSERT_EQ(Operation.Calls.size(), 2u);
    EXPECT_EQ(Operation.Calls[1].Amount, 9);

    // Recompiled once for the reload, not once per activation.
    const auto& store = WorldState.GetResource<VerbRelayBindingStore>();
    EXPECT_EQ(store.RebuildCount(), 2u);
    ASSERT_EQ(Activate(relay, relay), VerbAdmission::Accepted);
    Tick();
    EXPECT_EQ(store.RebuildCount(), 2u);
}

TEST_F(VerbRelayFixture, ARelayNamingNoResolvableBindingIsAbandonedWithADiagnostic)
{
    const EntityId relay = WorldState.CreateEntity();
    VerbRelay component;
    component.Bindings = Library;
    component.Binding = MakeVerbBindingKey("relay.never_authored");
    WorldState.AddComponent<VerbRelay>(relay, component);

    ASSERT_EQ(Activate(relay, relay), VerbAdmission::Accepted);
    Tick();
    EXPECT_TRUE(Operation.Calls.empty());
    EXPECT_EQ(Relay->Stats().Abandoned, 1u);
    bool traced = false;
    for (const VerbTraceRecord& record : Trace.Snapshot())
        traced = traced || (record.Event == VerbTraceEvent::Abandoned
                            && record.Status == VerbAdmission::UnresolvedBinding);
    EXPECT_TRUE(traced);
}

TEST_F(VerbRelayFixture, ShutdownDropsWhatIsStillQueued)
{
    const EntityId relay = PlaceRelay();
    ASSERT_EQ(Activate(relay, relay), VerbAdmission::Accepted);
    ASSERT_EQ(Relay->Pending(), 1u);
    Relay->Shutdown();
    EXPECT_EQ(Relay->Pending(), 0u);
    Tick();
    EXPECT_TRUE(Operation.Calls.empty());
}

// At namespace scope: a runtime-only component's identity is a specialization,
// and a specialization cannot be declared inside an unnamed namespace.
struct RelayTestMarker
{
};
SENCHA_DECLARE_COMPONENT_TYPE(RelayTestMarker, "test.relay_marker");

namespace
{
using Marker = RelayTestMarker;

// A deferred operation whose work is structural: it records what it will do
// while a query is live, and applies it only once the query scope has ended.
// The relay drain is not inside a query, but the operation cannot know what
// its caller is inside of, so the buffer is the contract either way.

class DespawnOperation
{
public:
    explicit DespawnOperation(World& world) : Commands(world), WorldRef(&world) {}

    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        EntityId target;
        if (!invocation.Arguments->TryGetEntity(0, target))
            return VerbAdmission::InvalidArguments;
        Pending.push_back(target);
        return VerbAdmission::Accepted;
    }

    // What a consuming system does at its own phase: walk a query, decide
    // against live rows, record, then flush after the scope closes.
    void Apply()
    {
        Query<Read<Marker>> marked(*WorldRef);
        marked.ForEachChunk([this](auto& view) {
            for (std::uint32_t row = 0; row < view.Count(); ++row)
            {
                const EntityId entity = view.Entity(row);
                for (const EntityId target : Pending)
                {
                    if (target == entity)
                        Commands.DestroyEntity(entity);
                }
            }
        });
        Pending.clear();
        Commands.Flush();
    }

    CommandBuffer Commands;
    World* WorldRef;
    std::vector<EntityId> Pending;
};
}

TEST_F(VerbRelayFixture, AStructuralOperationRecordsDuringTheQueryAndFlushesAfterIt)
{
    ComponentRegistrar registrar(WorldState);
    registrar.Add<Marker>();

    {
        VerbRegistrationScope scope(*Verbs, "test");
        VerbDefinition despawn;
        despawn.Name = "test.despawn";
        DataFieldSchema target;
        target.Key = "Target";
        target.Kind = DataFieldKind::Entity;
        despawn.Arguments.Children.push_back(std::move(target));
        (void)scope.Declare(std::move(despawn));
        ASSERT_TRUE(scope.Commit());
    }
    auto library = std::make_shared<VerbBindingLibrary>();
    VerbBindingDesc desc;
    desc.Key = "relay.despawn";
    desc.KeyId = MakeVerbBindingKey(desc.Key);
    desc.VerbName = "test.despawn";
    desc.Inputs = { "target" };
    VerbBindingArgument argument;
    argument.Key = "Target";
    argument.Source = VerbArgumentSource::Input;
    argument.Text = "target";
    desc.Arguments.push_back(std::move(argument));
    library->Bindings.push_back(std::move(desc));
    const DataAssetHandle handle = Cache.Register("asset://data/test/despawn.sdata",
                                                  std::string(kVerbBindingsTypeName), library);

    DespawnOperation despawn(WorldState);
    const VerbBindingToken token = Dispatcher->Bind(Verbs->Find("test.despawn"), despawn);

    const EntityId relay = WorldState.CreateEntity();
    VerbRelay component;
    component.Bindings = handle;
    component.Binding = MakeVerbBindingKey("relay.despawn");
    WorldState.AddComponent<VerbRelay>(relay, component);

    const EntityId doomed = WorldState.CreateEntity();
    WorldState.AddComponent<Marker>(doomed);
    const EntityId spared = WorldState.CreateEntity();
    WorldState.AddComponent<Marker>(spared);

    ASSERT_EQ(Activate(relay, doomed), VerbAdmission::Accepted);
    Tick();
    ASSERT_EQ(despawn.Pending.size(), 1u);
    // Admitted, not executed: the drain does no structural work of its own.
    EXPECT_TRUE(WorldState.IsAlive(doomed));

    despawn.Apply();
    EXPECT_FALSE(WorldState.IsAlive(doomed));
    EXPECT_TRUE(WorldState.IsAlive(spared));
    EXPECT_TRUE(WorldState.IsAlive(relay));
}
