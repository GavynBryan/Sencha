#include <authored/AuthoredEventDispatcher.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/Counter.h>
#include <logic/CounterVocabulary.h>
#include <net/NetReplicationComponents.h>
#include <save/HeldRecordSaves.h>
#include <world/ComponentRegistrar.h>
#include <world/SimulationAuthority.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    struct WarningSink final : ILogSink
    {
        void Write(LogLevel level, std::string_view, std::string_view message) override
        {
            if (level == LogLevel::Warning)
                Warnings.emplace_back(message);
        }

        std::vector<std::string> Warnings;
    };

    struct Heard
    {
        GameplayTagId Name;
        std::int64_t Value = 0;
        EntityId Holder;
        EntityId Source;
    };

    struct Listener
    {
        static void Hear(Listener& self, const AuthoredEventDelivery& delivery)
        {
            Heard heard;
            heard.Source = delivery.Source;
            (void)delivery.Payload->TryGetTag(0, heard.Name);
            (void)delivery.Payload->TryGetInt(1, heard.Value);
            (void)delivery.Payload->TryGetEntity(2, heard.Holder);
            self.Occurrences.push_back(heard);
        }

        std::vector<Heard> Occurrences;
    };

    struct CounterWorld
    {
        CounterWorld()
            : Sink(Logging.AddSink<WarningSink>())
            , Context(Logging)
        {
            ComponentRegistrar registrar(&Schema, &Serializers, nullptr);
            registrar.Add<PersistentIdComponent>();
            registrar.Add<Held>();
            registrar.Add<Counter>();
            registrar.Add<NetReplicated>();
            registrar.Add<NetRelevanceAnchor>();
            Schema.Seal();
            Schema.Apply(Entities);
            Entities.AddResource<PersistentEntityIndex>();
            Index = &InstallHeldRecords(Entities);

            GameplayTagRegistry& tags = Entities.AddResource<GameplayTagRegistry>();
            Boss = *tags.RegisterTag("boss.warden.defeated");
            RedCoins = *tags.RegisterTag("star.course3.red_coins");
            Quest = *tags.RegisterTag("quest.lighthouse.stage");

            InstallAuthoredVocabulary(Entities);
            VerbRegistrationScope verbs(*FindVerbRegistry(Entities), "engine");
            AuthoredQueryRegistrationScope queries(*FindAuthoredQueryRegistry(Entities), "engine");
            AuthoredEventRegistrationScope events(*FindAuthoredEventRegistry(Entities), "engine");
            DeclareCounterVocabulary(verbs, queries, events);
            EXPECT_TRUE(CommitTogether(verbs, queries, events));

            Events = std::make_unique<AuthoredEventDispatcher>(*FindAuthoredEventRegistry(Entities),
                                                               Logging.GetLogger<CounterWorld>());
            Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(Entities));
            ValueToken = BindCounterQuery(*Queries, Entities);
            Operations = std::make_unique<CounterOperations>(Entities, Logging.GetLogger<CounterOperations>(),
                                                             Events.get());
            Subscription = Events->Subscribe<&Listener::Hear>(
                FindAuthoredEventRegistry(Entities)->Resolve(kCounterChangedEvent), EntityId{}, Heard);
        }

        ~CounterWorld()
        {
            Subscription.Reset();
            ValueToken.Reset();
        }

        static AuthoredArguments Arguments(GameplayTagId name, std::int64_t number, std::optional<EntityId> holder)
        {
            AuthoredArguments arguments;
            arguments.Resize(3);
            arguments.Set(0, AuthoredValue::Tag(name));
            arguments.Set(1, AuthoredValue::Int(number));
            arguments.Set(2, holder.has_value() ? AuthoredValue::Entity(*holder) : AuthoredValue{});
            return arguments;
        }

        VerbAdmission Set(GameplayTagId name, std::int64_t value, std::optional<EntityId> holder = {})
        {
            const AuthoredArguments arguments = Arguments(name, value, holder);
            VerbInvocation invocation;
            invocation.Arguments = &arguments;
            return Operations->Set(invocation);
        }

        VerbAdmission Add(GameplayTagId name, std::int64_t amount, std::optional<EntityId> holder = {})
        {
            const AuthoredArguments arguments = Arguments(name, amount, holder);
            VerbInvocation invocation;
            invocation.Arguments = &arguments;
            return Operations->Add(invocation);
        }

        std::optional<std::int64_t> Value(GameplayTagId name, std::optional<EntityId> holder = {})
        {
            const AuthoredValue arguments[] = { AuthoredValue::Tag(name),
                                                holder.has_value() ? AuthoredValue::Entity(*holder) : AuthoredValue{} };
            AuthoredValue result;
            std::int64_t value = 0;
            if (Queries->Evaluate(FindAuthoredQueryRegistry(Entities)->Resolve(kCounterValueQuery), arguments, result)
                    != AuthoredQueryStatus::Value
                || !result.TryGetInt(value))
                return std::nullopt;
            return value;
        }

        EntityId Body(PersistentEntityId id = {})
        {
            const EntityId body = Entities.CreateEntity();
            if (id.IsValid())
                Entities.AddComponent<PersistentIdComponent>(body, PersistentIdComponent{ id });
            return body;
        }

        JsonValue Capture(std::span<const EntityId> roots)
        {
            return CaptureHeldRecords(Entities, roots, Serializers, Context);
        }

        void Restore(const JsonValue& saved) { RestoreHeldRecords(Entities, saved, Serializers, Context); }

        LoggingProvider Logging;
        WarningSink& Sink;
        SceneSerializationContext Context;
        WorldComponentSchema Schema;
        ComponentSerializerRegistry Serializers;
        World Entities;
        HeldRecordIndex* Index = nullptr;
        GameplayTagId Boss;
        GameplayTagId RedCoins;
        GameplayTagId Quest;
        std::unique_ptr<AuthoredEventDispatcher> Events;
        std::unique_ptr<AuthoredQueryDispatcher> Queries;
        std::unique_ptr<CounterOperations> Operations;
        AuthoredQueryBindingToken ValueToken;
        AuthoredEventSubscription Subscription;
        Listener Heard;
    };
}

TEST(Counters, AnUnsetCounterIsZeroAndASetOneKeepsItsValue)
{
    CounterWorld world;
    EXPECT_EQ(world.Value(world.Boss), 0);

    EXPECT_EQ(world.Set(world.Boss, 1), VerbAdmission::Accepted);
    EXPECT_EQ(world.Value(world.Boss), 1);
    EXPECT_EQ(world.Add(world.Quest, 3), VerbAdmission::Accepted);
    EXPECT_EQ(world.Add(world.Quest, 2), VerbAdmission::Accepted);
    EXPECT_EQ(world.Value(world.Quest), 5);
    EXPECT_EQ(world.Index->RecordsOf(EntityId{}).size(), 2u) << "one record per counter that is not zero";
}

TEST(Counters, ReachingZeroRemovesTheRecord)
{
    CounterWorld world;
    (void)world.Set(world.RedCoins, 2);
    (void)world.Add(world.RedCoins, -2);
    EXPECT_EQ(world.Value(world.RedCoins), 0);
    EXPECT_TRUE(world.Index->RecordsOf(EntityId{}).empty());

    (void)world.Set(world.Boss, 1);
    (void)world.Set(world.Boss, 0);
    EXPECT_EQ(world.Index->Count(), 0u);
}

TEST(Counters, ABodysCounterIsItsOwnAndGoesWithIt)
{
    CounterWorld world;
    const EntityId body = world.Body();
    const EntityId other = world.Body();
    (void)world.Add(world.RedCoins, 4, body);
    (void)world.Add(world.RedCoins, 1);

    EXPECT_EQ(world.Value(world.RedCoins, body), 4);
    EXPECT_EQ(world.Value(world.RedCoins, other), 0);
    EXPECT_EQ(world.Value(world.RedCoins), 1);

    world.Entities.DestroyEntity(body);
    SettleHeldRecords(world.Entities);
    EXPECT_EQ(world.Index->Count(), 1u) << "the body's counter went with it; the world's stays";
}

TEST(Counters, AChangeIsAnnouncedWithItsHolderAndNothingIsAnnouncedForNoChange)
{
    CounterWorld world;
    const EntityId body = world.Body();
    (void)world.Set(world.Quest, 2, body);
    (void)world.Set(world.Quest, 2, body);
    (void)world.Add(world.Boss, 1);
    (void)world.Set(world.Boss, 0);
    (void)world.Events->Drain(1);

    ASSERT_EQ(world.Heard.Occurrences.size(), 3u);
    EXPECT_EQ(world.Heard.Occurrences[0].Name, world.Quest);
    EXPECT_EQ(world.Heard.Occurrences[0].Value, 2);
    EXPECT_EQ(world.Heard.Occurrences[0].Holder, body);
    EXPECT_EQ(world.Heard.Occurrences[0].Source, body) << "a subscriber can listen to one body's counters";
    EXPECT_FALSE(world.Heard.Occurrences[1].Holder.IsValid());
    EXPECT_EQ(world.Heard.Occurrences[2].Value, 0) << "unsetting is a change to zero";
}

TEST(Counters, ValuesAreHeldAtTheirBoundsCountedAndLoggedOnceAtEachEnd)
{
    CounterWorld world;
    constexpr std::int64_t kLargest = std::numeric_limits<std::int32_t>::max();
    constexpr std::int64_t kSmallest = std::numeric_limits<std::int32_t>::min();

    (void)world.Set(world.RedCoins, kLargest);
    EXPECT_EQ(world.Value(world.RedCoins), kLargest);
    EXPECT_EQ(world.Entities.TryGetResource<CounterSaturation>(), nullptr) << "the bound itself is a value";

    (void)world.Add(world.RedCoins, 1);
    (void)world.Add(world.RedCoins, std::numeric_limits<std::int64_t>::max());
    EXPECT_EQ(world.Value(world.RedCoins), kLargest);
    (void)world.Set(world.Boss, kSmallest);
    (void)world.Add(world.Boss, -1);
    (void)world.Set(world.Quest, kSmallest - 1);
    EXPECT_EQ(world.Value(world.Boss), kSmallest);

    const CounterSaturation& saturation = world.Entities.GetResource<CounterSaturation>();
    EXPECT_EQ(saturation.AtMaximum, 2u);
    EXPECT_EQ(saturation.AtMinimum, 2u);
    EXPECT_EQ(world.Sink.Warnings.size(), 2u) << "once at each bound";
}

TEST(Counters, OnlyTheAuthorityChangesACounterAndAGoneHolderIsRefused)
{
    CounterWorld world;
    const EntityId body = world.Body();
    world.Entities.DestroyEntity(body);
    EXPECT_EQ(world.Set(world.Boss, 1, body), VerbAdmission::InvalidArguments);
    EXPECT_EQ(world.Value(world.Boss, body), std::nullopt);

    world.Entities.AddResource<SimulationAuthority>().Authoritative = false;
    EXPECT_EQ(world.Set(world.Boss, 1), VerbAdmission::Refused);
    EXPECT_EQ(world.Index->Count(), 0u);
}

TEST(Counters, ARestoreBringsBackExactlyWhatWasSavedUnderTheSameIdsAndHolders)
{
    CounterWorld world;
    const EntityId body = world.Body(PersistentEntityId{ 0x77 });
    (void)world.Set(world.Boss, 1);
    (void)world.Set(world.RedCoins, 6, body);
    const EntityId boss = FindCounter(world.Entities, world.Boss, {});
    const PersistentEntityId bossId = world.Entities.TryGet<PersistentIdComponent>(boss)->Id;
    const JsonValue saved = world.Capture(std::span(&body, 1));

    (void)world.Set(world.Boss, 0);
    (void)world.Set(world.Quest, 3);
    (void)world.Add(world.RedCoins, 10, body);
    world.Restore(saved);
    SettleHeldRecords(world.Entities);

    EXPECT_EQ(world.Value(world.Boss), 1);
    EXPECT_EQ(world.Value(world.Quest), 0);
    EXPECT_EQ(world.Value(world.RedCoins, body), 6);
    EXPECT_EQ(world.Index->Count(), 2u);
    const EntityId restored = FindCounter(world.Entities, world.Boss, {});
    EXPECT_EQ(world.Entities.TryGet<PersistentIdComponent>(restored)->Id, bossId);
    EXPECT_EQ(world.Entities.GetResource<PersistentEntityIndex>().CollisionCount(), 0u);
}

TEST(Counters, AFreshWorldRestoresTheWorldsAtOnceAndTheBodysWhenItArrives)
{
    JsonValue saved;
    PersistentEntityId coinsId;
    {
        CounterWorld before;
        const EntityId body = before.Body(PersistentEntityId{ 0x51 });
        (void)before.Set(before.Quest, 4);
        (void)before.Add(before.RedCoins, 8, body);
        coinsId = before.Entities.TryGet<PersistentIdComponent>(FindCounter(before.Entities, before.RedCoins, body))->Id;
        saved = before.Capture(std::span(&body, 1));
    }

    CounterWorld after;
    after.Restore(saved);
    SettleHeldRecords(after.Entities);
    EXPECT_EQ(after.Value(after.Quest), 4);
    ASSERT_EQ(after.Index->Count(), 2u);

    const EntityId body = after.Body(PersistentEntityId{ 0x51 });
    SettleHeldRecords(after.Entities);
    EXPECT_EQ(after.Value(after.RedCoins, body), 8);
    const EntityId coins = FindCounter(after.Entities, after.RedCoins, body);
    EXPECT_EQ(after.Entities.TryGet<PersistentIdComponent>(coins)->Id, coinsId);
    EXPECT_EQ(after.Entities.TryGet<NetRelevanceAnchor>(coins)->Anchor, body);
}

TEST(Counters, ACounterNamingAnUndeclaredTagIsNotRestored)
{
    JsonValue saved;
    {
        CounterWorld before;
        (void)before.Set(before.Boss, 1);
        (void)before.Set(before.Quest, 2);
        saved = before.Capture({});
    }
    CounterWorld after;
    for (JsonValue& record : saved.Find("records")->AsArray())
        if (JsonValue* counter = record.Find("components")->Find("Counter");
            counter != nullptr && counter->Find("name")->AsString() == "boss.warden.defeated")
            *counter->Find("name") = JsonValue(std::string("boss.retired"));

    after.Restore(saved);

    EXPECT_EQ(after.Index->Count(), 1u);
    EXPECT_EQ(after.Value(after.Quest), 2);
    EXPECT_FALSE(after.Sink.Warnings.empty());
}

TEST(Counters, ACounterIsFoundUnderTheHolderItIsListedUnderAndNeverUnderTheWorldWhileItWaits)
{
    CounterWorld world;
    const EntityId body = world.Body(PersistentEntityId{ 0x33 });
    (void)world.Set(world.Boss, 2, body);
    const EntityId record = FindCounter(world.Entities, world.Boss, body);
    ASSERT_TRUE(record.IsValid());

    world.Index->Await(record, PersistentEntityId{ 0x99 });
    EXPECT_FALSE(FindCounter(world.Entities, world.Boss, body).IsValid());
    EXPECT_FALSE(FindCounter(world.Entities, world.Boss, EntityId{}).IsValid()) << "waiting is not the world";

    const EntityId other = world.Body(PersistentEntityId{ 0x99 });
    EXPECT_EQ(FindCounter(world.Entities, world.Boss, other), record);
    EXPECT_EQ(world.Value(world.Boss, other), 2);
}

TEST(Counters, ANameArrivingFromTheWireIsTheNameTheCounterIsFoundBy)
{
    CounterWorld world;
    (void)world.Set(world.Boss, 5);
    const EntityId record = FindCounter(world.Entities, world.Boss, EntityId{});
    Counter renamed{ .Name = world.Quest, .Value = 5 };
    std::byte bytes[sizeof(Counter)];
    std::memcpy(bytes, &renamed, sizeof(Counter));

    ASSERT_TRUE(world.Schema.SetComponentBytes(world.Entities, record, ResolveComponentTypeId<Counter>(), bytes));

    EXPECT_FALSE(FindCounter(world.Entities, world.Boss, EntityId{}).IsValid());
    EXPECT_EQ(FindCounter(world.Entities, world.Quest, EntityId{}), record);
}

TEST(Counters, ACounterReplacedInPlaceUnderItsOwnNameIsStillFoundByIt)
{
    CounterWorld world;
    (void)world.Set(world.Boss, 5);
    const EntityId record = FindCounter(world.Entities, world.Boss, EntityId{});
    ASSERT_TRUE(record.IsValid());

    ASSERT_TRUE(world.Entities.ReplaceComponent<Counter>(record, Counter{ .Name = world.Boss, .Value = 6 }));

    EXPECT_EQ(FindCounter(world.Entities, world.Boss, EntityId{}), record);
    EXPECT_EQ(world.Value(world.Boss), 6);
    world.Entities.DestroyEntity(record);
    EXPECT_FALSE(FindCounter(world.Entities, world.Boss, EntityId{}).IsValid()) << "and its one key goes with it";
}

TEST(Counters, ACounterReplacedInPlaceUnderAnotherNameIsFoundOnlyByTheNewOne)
{
    CounterWorld world;
    (void)world.Set(world.Boss, 5);
    const EntityId record = FindCounter(world.Entities, world.Boss, EntityId{});
    ASSERT_TRUE(record.IsValid());

    ASSERT_TRUE(world.Entities.ReplaceComponent<Counter>(record, Counter{ .Name = world.Quest, .Value = 5 }));

    EXPECT_FALSE(FindCounter(world.Entities, world.Boss, EntityId{}).IsValid());
    EXPECT_EQ(FindCounter(world.Entities, world.Quest, EntityId{}), record);
    world.Entities.DestroyEntity(record);
    EXPECT_FALSE(FindCounter(world.Entities, world.Quest, EntityId{}).IsValid());
}
