// What a targeted delivery costs, and what it must not do.
//
// Timings are printed, not asserted: a Debug build's numbers describe the shape
// for a handoff, not a gate. What is asserted is the shape the design promises:
// a delivery to an entity without Responses, or with none that match, encodes
// nothing, queues nothing and allocates nothing; a matching delivery encodes
// once and, warmed, allocates nothing to admit or to run; and global
// subscribers to the same event change none of it, because Deliver never
// touches the dispatcher. The queue and the budget are sized so that no
// admitted work is dropped or left waiting.

#include "AllocationCounter.h"

#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredEventDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleRegistry.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/ResponseSet.h>
#include <logic/ResponseVerbs.h>
#include <logic/Responses.h>
#include <logic/ResponsesSystem.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/SimClock.h>
#include <world/ComponentRegistrar.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
struct BenchStruck
{
    EntityId Instigator;
    GameplayTagId Attack;
};
} // namespace

template <>
struct AuthoredApiDefinition<BenchStruck>
{
    static constexpr std::string_view EventName = "bench.struck";
    static void Encode(const BenchStruck& event, AuthoredArguments& payload)
    {
        payload.Resize(2);
        payload.Set(0, AuthoredValue::Entity(event.Instigator));
        payload.Set(1, AuthoredValue::Tag(event.Attack));
    }
};

namespace
{
constexpr int kDeliveries = 10'000;
constexpr int kSubscribers = 100;

class NullOperation
{
public:
    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        EntityId entity;
        Sum += invocation.Arguments->TryGetEntity(0, entity) ? entity.Index : 0;
        return VerbAdmission::Accepted;
    }
    std::uint64_t Sum = 0;
};

struct Listener
{
    std::uint64_t Heard = 0;
    static void Hear(Listener& self, const AuthoredEventDelivery&) { ++self.Heard; }
};

struct Measured
{
    double DeliverNanos = 0.0;
    double PassNanos = 0.0;
    std::size_t DeliverAllocations = 0;
    std::size_t PassAllocations = 0;
    std::size_t QueueBytes = 0;
    ResponsesSystem::Counters Counts;
};

class DeliveryBench
{
public:
    DeliveryBench()
    {
        ComponentRegistrar registrar(Entities);
        registrar.Add<Responses>();
        Tags = &Entities.AddResource<GameplayTagRegistry>();
        Charge = *Tags->RegisterTag("Bench.Attack.Charge");
        Default = *Tags->RegisterTag("Bench.Attack.Default");

        InstallAuthoredVocabulary(Entities);
        VerbRegistrationScope verbs(*FindVerbRegistry(Entities), "bench");
        AuthoredEventRegistrationScope events(*FindAuthoredEventRegistry(Entities), "bench");
        VerbDefinition nothing;
        nothing.Name = "bench.nothing";
        nothing.Arguments.Children.push_back(MakeDataField(DataFieldKind::Entity, "target", "Target"));
        (void)verbs.Declare(std::move(nothing));
        DeclareResponseVocabulary(verbs, events);
        AuthoredEventDefinition struck;
        struck.Name = "bench.struck";
        struck.Payload.Children.push_back(MakeDataField(DataFieldKind::Entity, "instigator", "Instigator"));
        struck.Payload.Children.push_back(MakeDataField(DataFieldKind::GameplayTag, "attack", "Attack"));
        struck.Instigator = "instigator";
        (void)events.Declare(std::move(struck));
        AuthoredEventDefinition used;
        used.Name = "bench.used";
        used.Payload.Children.push_back(MakeDataField(DataFieldKind::Entity, "user", "User"));
        (void)events.Declare(std::move(used));
        EXPECT_TRUE(CommitTogether(verbs, events));

        Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(Entities));
        Token = Verbs->Bind(FindVerbRegistry(Entities)->Find("bench.nothing"), Nothing);
        Events = std::make_unique<AuthoredEventDispatcher>(*FindAuthoredEventRegistry(Entities),
                                                           Logging.GetLogger<DeliveryBench>());
        Logic.Add(StoragePartitionId::Default());
        System = &RegisterResponsesSystem(Schedule, Entities, Clock, *Verbs, nullptr, nullptr, nullptr, Console,
                                          Logging);
        System->SetCapacity(4 * kDeliveries);
        System->SetCommandBudget(1u << 20);
        Schedule.Init();
    }

    ~DeliveryBench()
    {
        Schedule.Shutdown();
        Subscriptions.clear();
        Token.Reset();
    }

    // kDeliveries entities sharing one set, as prefab instances do; none for an empty text.
    std::vector<EntityId> Place(const char* bindings)
    {
        std::vector<EntityId> placed;
        const std::optional<JsonValue> parsed = bindings != nullptr ? JsonParse(bindings) : std::nullopt;
        for (int i = 0; i < kDeliveries; ++i)
        {
            const EntityId entity = Entities.CreateEntity();
            if (parsed)
            {
                Entities.AddComponent<Responses>(entity, Responses{});
                EXPECT_TRUE(SetResponseBindings(Entities, entity, *parsed));
            }
            placed.push_back(entity);
        }
        return placed;
    }

    void Subscribe()
    {
        const AuthoredEventHandle handle = FindAuthoredEventRegistry(Entities)->Resolve("bench.struck");
        for (int i = 0; i < kSubscribers; ++i)
            Subscriptions.push_back(Events->Subscribe<&Listener::Hear>(handle, EntityId{}, Heard));
    }

    void Pass()
    {
        Clock.EndFixedTick();
        FixedLogicContext fixed{
            .Config = Config,
            .Runtime = Runtime,
            .Time = FixedSimTime{ .DeltaSeconds = Clock.GetFixedDt(), .TickIndex = Clock.GetTickIndex() },
            .Entities = Entities,
            .Partitions = Logic,
        };
        System->FixedLogic(fixed);
    }

    // One round warms every buffer; the second is measured.
    Measured Run(const std::vector<EntityId>& subjects, GameplayTagId attack)
    {
        Measured measured;
        for (int round = 0; round < 2; ++round)
        {
            Pass();
            const ResponsesSystem::Counters before = System->Stats();
            const std::size_t allocations = AllocationCount();
            const auto start = std::chrono::steady_clock::now();
            for (const EntityId subject : subjects)
                (void)System->Deliver(subject, BenchStruck{ subject, attack });
            const auto delivered = std::chrono::steady_clock::now();
            measured.DeliverAllocations = AllocationCount() - allocations;
            measured.QueueBytes = System->QueueBytes();

            const std::size_t passAllocations = AllocationCount();
            Pass();
            const auto ran = std::chrono::steady_clock::now();
            measured.PassAllocations = AllocationCount() - passAllocations;
            measured.DeliverNanos = std::chrono::duration<double, std::nano>(delivered - start).count() / kDeliveries;
            measured.PassNanos = std::chrono::duration<double, std::nano>(ran - delivered).count() / kDeliveries;
            const ResponsesSystem::Counters& after = System->Stats();
            measured.Counts = ResponsesSystem::Counters{
                .Delivered = after.Delivered - before.Delivered,
                .Admitted = after.Admitted - before.Admitted,
                .Encoded = after.Encoded - before.Encoded,
                .Gated = after.Gated - before.Gated,
                .Activations = after.Activations - before.Activations,
                .Invoked = after.Invoked - before.Invoked,
                .Failed = after.Failed - before.Failed,
                .Refused = after.Refused - before.Refused,
                .Abandoned = after.Abandoned - before.Abandoned,
            };
            EXPECT_EQ(System->Pending(), 0u) << "admitted work was left waiting";
        }
        return measured;
    }

    LoggingProvider Logging;
    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    ConsoleRegistry Console;
    World Entities;
    StoragePartitionSet Logic;
    EngineSchedule Schedule;
    FixedSimulationLoop Clock;
    GameplayTagRegistry* Tags = nullptr;
    GameplayTagId Charge;
    GameplayTagId Default;
    std::unique_ptr<VerbDispatcher> Verbs;
    std::unique_ptr<AuthoredEventDispatcher> Events;
    NullOperation Nothing;
    VerbBindingToken Token;
    Listener Heard;
    std::vector<AuthoredEventSubscription> Subscriptions;
    ResponsesSystem* System = nullptr;
};

void Report(const char* name, const Measured& m)
{
    std::printf("[responses bench] %-34s deliver %8.1f ns  pass %8.1f ns  allocs deliver %zu pass %zu  "
                "queue %zu B  delivered %llu admitted %llu encoded %llu gated %llu ran %llu invoked %llu "
                "refused %llu\n",
                name, m.DeliverNanos, m.PassNanos, m.DeliverAllocations, m.PassAllocations, m.QueueBytes,
                static_cast<unsigned long long>(m.Counts.Delivered), static_cast<unsigned long long>(m.Counts.Admitted),
                static_cast<unsigned long long>(m.Counts.Encoded), static_cast<unsigned long long>(m.Counts.Gated),
                static_cast<unsigned long long>(m.Counts.Activations),
                static_cast<unsigned long long>(m.Counts.Invoked), static_cast<unsigned long long>(m.Counts.Refused));
}

constexpr const char* kOtherEvent = R"({"use": {"on": "bench.used", "sequence": {"commands": []}}})";
constexpr const char* kConditionFails = R"({"hit": {"on": "bench.struck", "sequence": {
    "conditions": [{"tag": "attack", "tags": {"any": ["Bench.Attack.Charge"]}}],
    "commands": [{"verb": "bench.nothing", "arguments": {"target": {"port": "self"}}}]}}})";
constexpr const char* kCommandsRun = R"({"hit": {"on": "bench.struck", "sequence": {
    "commands": [{"verb": "bench.nothing", "arguments": {"target": {"port": "self"}}}]}}})";

void RunAll(bool subscribed)
{
    DeliveryBench bench;
    if (subscribed)
        bench.Subscribe();
    const char* suffix = subscribed ? " +100 subscribers" : "";

    const Measured none = bench.Run(bench.Place(nullptr), bench.Charge);
    Report((std::string("no Responses") + suffix).c_str(), none);
    EXPECT_EQ(none.Counts.Delivered, 0u);
    EXPECT_EQ(none.Counts.Encoded, 0u);
    EXPECT_EQ(none.DeliverAllocations, 0u) << "a delivery to an entity without responses allocated";

    const Measured other = bench.Run(bench.Place(kOtherEvent), bench.Charge);
    Report((std::string("Responses, no matching event") + suffix).c_str(), other);
    EXPECT_EQ(other.Counts.Delivered, static_cast<std::uint64_t>(kDeliveries));
    EXPECT_EQ(other.Counts.Encoded, 0u) << "a delivery no binding answers was encoded";
    EXPECT_EQ(other.Counts.Admitted, 0u);
    EXPECT_EQ(other.DeliverAllocations, 0u);

    const Measured gated = bench.Run(bench.Place(kConditionFails), bench.Default);
    Report((std::string("match, condition fails") + suffix).c_str(), gated);
    EXPECT_EQ(gated.Counts.Encoded, static_cast<std::uint64_t>(kDeliveries));
    EXPECT_EQ(gated.Counts.Admitted, static_cast<std::uint64_t>(kDeliveries));
    EXPECT_EQ(gated.Counts.Gated, static_cast<std::uint64_t>(kDeliveries));
    EXPECT_EQ(gated.Counts.Refused, 0u);
    EXPECT_EQ(gated.DeliverAllocations, 0u) << "a warmed matching delivery allocated";
    EXPECT_EQ(gated.PassAllocations, 0u) << "a warmed pass of failing conditions allocated";

    const Measured ran = bench.Run(bench.Place(kCommandsRun), bench.Charge);
    Report((std::string("match, commands run") + suffix).c_str(), ran);
    EXPECT_EQ(ran.Counts.Activations, static_cast<std::uint64_t>(kDeliveries));
    EXPECT_EQ(ran.Counts.Invoked, static_cast<std::uint64_t>(kDeliveries));
    EXPECT_EQ(ran.Counts.Refused, 0u);
    EXPECT_EQ(ran.DeliverAllocations, 0u);
    EXPECT_EQ(ran.PassAllocations, 0u) << "a warmed pass that runs commands allocated";

    EXPECT_EQ(bench.Events->Queued(), 0u) << "Deliver reached the global dispatcher";
    EXPECT_EQ(bench.Heard.Heard, 0u);
}
} // namespace

TEST(ResponsesDeliveryBench, TargetedDeliveryCostsWhatTheEntityAnswers)
{
    RunAll(false);
}

TEST(ResponsesDeliveryBench, GlobalSubscribersChangeNothingAboutTargetedDelivery)
{
    RunAll(true);
}

// For comparison: the dispatcher's global path encodes, validates and queues
// every publish, with or without anyone listening.
TEST(ResponsesDeliveryBench, ReferenceGlobalPublish)
{
    for (const bool subscribed : { false, true })
    {
        DeliveryBench bench;
        bench.Events->SetCapacity(2 * kDeliveries);
        bench.Events->SetBudget(static_cast<std::size_t>(kSubscribers + 1) * 2 * kDeliveries);
        if (subscribed)
            bench.Subscribe();
        double publishNanos = 0.0;
        double drainNanos = 0.0;
        std::size_t allocations = 0;
        for (int round = 0; round < 2; ++round)
        {
            const std::size_t before = AllocationCount();
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < kDeliveries; ++i)
                (void)bench.Events->Publish(EntityId{}, BenchStruck{ EntityId{}, bench.Charge });
            const auto published = std::chrono::steady_clock::now();
            const AuthoredEventDrainResult drained = bench.Events->Drain(static_cast<std::uint64_t>(round + 1));
            EXPECT_EQ(drained.Remaining, 0u) << "the reference drain stopped short";
            const auto finished = std::chrono::steady_clock::now();
            allocations = AllocationCount() - before;
            publishNanos = std::chrono::duration<double, std::nano>(published - start).count() / kDeliveries;
            drainNanos = std::chrono::duration<double, std::nano>(finished - published).count() / kDeliveries;
        }
        std::printf("[responses bench] %-34s publish %8.1f ns  drain %8.1f ns  allocs %zu\n",
                    subscribed ? "Publish +100 subscribers" : "Publish, no subscribers", publishNanos, drainNanos,
                    allocations);
    }
}
