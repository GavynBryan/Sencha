// Memory keeps where a subject was, measures how it moved, forgets on a
// clock, and answers its authored queries only on the authority.

#include "PerceptionHarness.h"

#include <authored/AuthoredQueryDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <perception/ObservationMemory.h>
#include <perception/ObservationMemorySystem.h>
#include <world/SimulationAuthority.h>

#include <optional>
#include <vector>

namespace
{
    constexpr double kDt = 1.0 / 60.0;

    struct MemoryHarness : PerceptionHarness
    {
        ObservationMemorySystem Memory;
        std::optional<AuthoredQueryDispatcher> Dispatcher;
        std::vector<AuthoredQueryBindingToken> Bindings;

        MemoryHarness()
        {
            Entities.RegisterComponent<ObservationMemory>();
            InstallAuthoredVocabulary(Entities);
            EXPECT_TRUE(InstallPerceptionVocabulary(Entities));
            Dispatcher.emplace(*FindAuthoredQueryRegistry(Entities));
            Bindings = BindPerceptionQueries(*Dispatcher, Entities);
        }

        EntityId RememberingObserver(const Vec3d& position, float retainSeconds = 2.0f)
        {
            const EntityId entity = Observer(position);
            Entities.AddComponent<ObservationMemory>(entity, ObservationMemory{ .RetainSeconds = retainSeconds });
            return entity;
        }

        void Tick()
        {
            FixedLogicContext ctx{
                .Config = Config,
                .Runtime = Runtime,
                .Time = FixedSimTime{ .DeltaSeconds = kDt, .TickIndex = ++PerceptionHarness::Tick },
                .Entities = Entities,
                .Partitions = Partitions,
            };
            System.FixedLogic(ctx);
            Memory.FixedLogic(ctx);
        }

        void Ticks(int count)
        {
            for (int index = 0; index < count; ++index)
                Tick();
        }

        const ObservationMemoryEntry* Entry(EntityId observer, EntityId subject) const
        {
            return FindRemembered(*Entities.TryGet<ObservationMemory>(observer), subject);
        }

        AuthoredQueryStatus Ask(std::string_view query, EntityId observer, EntityId subject, AuthoredValue& out)
        {
            const std::vector<AuthoredValue> arguments{ AuthoredValue::Entity(observer),
                                                        AuthoredValue::Entity(subject) };
            return Dispatcher->Evaluate(Dispatcher->Registry().Resolve(query), arguments, out);
        }
    };
}

TEST(ObservationMemory, KeepsWhereTheSubjectWasOnceItIsHidden)
{
    MemoryHarness harness;
    const EntityId eye = harness.RememberingObserver(Vec3d::Zero());
    const EntityId target = harness.Target(Vec3d(0, 0, -5));
    harness.Tick();
    ASSERT_NE(harness.Entry(eye, target), nullptr);

    harness.Place(target, Vec3d(0, 0, 5));
    harness.Ticks(10);
    const ObservationMemoryEntry* entry = harness.Entry(eye, target);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->Position, Vec3d(0, 0, -5));
    EXPECT_NEAR(entry->Age, 10 * kDt, 1e-4);
}

TEST(ObservationMemory, MeasuresVelocityFromConsecutiveObservations)
{
    MemoryHarness harness;
    const EntityId eye = harness.RememberingObserver(Vec3d::Zero());
    const EntityId target = harness.Target(Vec3d(0, 0, -10));
    harness.Tick();
    EXPECT_EQ(harness.Entry(eye, target)->Velocity, Vec3d::Zero());

    harness.Place(target, Vec3d(0.1f, 0, -10));
    harness.Tick();
    const ObservationMemoryEntry* entry = harness.Entry(eye, target);
    EXPECT_NEAR(entry->Velocity.X, 0.1 / kDt, 1e-2);
    EXPECT_NEAR(entry->Tracked, kDt, 1e-4);
}

TEST(ObservationMemory, ALapseResetsTrackingAndVelocity)
{
    MemoryHarness harness;
    const EntityId eye = harness.RememberingObserver(Vec3d::Zero());
    const EntityId target = harness.Target(Vec3d(0, 0, -10));
    harness.Ticks(5);
    harness.Place(target, Vec3d(0, 0, 10));
    harness.Ticks(30);
    harness.Place(target, Vec3d(1, 0, -10));
    harness.Tick();

    const ObservationMemoryEntry* entry = harness.Entry(eye, target);
    EXPECT_EQ(entry->Velocity, Vec3d::Zero());
    EXPECT_EQ(entry->Tracked, 0.0f);
    EXPECT_EQ(entry->Age, 0.0f);
}

TEST(ObservationMemory, ForgetsAfterItsRetention)
{
    MemoryHarness harness;
    const EntityId eye = harness.RememberingObserver(Vec3d::Zero(), 0.5f);
    const EntityId target = harness.Target(Vec3d(0, 0, -5));
    harness.Tick();
    harness.Place(target, Vec3d(0, 0, 5));
    harness.Ticks(29);
    EXPECT_NE(harness.Entry(eye, target), nullptr);
    harness.Ticks(2);
    EXPECT_EQ(harness.Entry(eye, target), nullptr);
}

TEST(ObservationMemory, AFullMemoryReplacesItsOldestEntry)
{
    MemoryHarness harness;
    const EntityId eye = harness.RememberingObserver(Vec3d::Zero(), 100.0f);
    std::vector<EntityId> targets;
    for (std::uint32_t index = 0; index < kObservationMemoryCapacity; ++index)
        targets.push_back(harness.Target(Vec3d(0.5f * static_cast<float>(index), 0, -10)));
    harness.Tick();
    for (std::uint32_t index = 1; index < kObservationMemoryCapacity; ++index)
        harness.Place(targets[index], Vec3d(0, 0, 5));
    harness.Place(targets[0], Vec3d(0, 0, 5));
    harness.Ticks(3);
    harness.Place(targets[3], Vec3d(1.5f, 0, -10));
    harness.Tick();

    const EntityId newcomer = harness.Target(Vec3d(-2, 0, -10));
    harness.Tick();
    EXPECT_NE(harness.Entry(eye, newcomer), nullptr);
    EXPECT_NE(harness.Entry(eye, targets[3]), nullptr);
    EXPECT_EQ(harness.Entities.TryGet<ObservationMemory>(eye)->Count, kObservationMemoryCapacity);
}

TEST(ObservationMemory, AnonymousObservationsNearbyRefreshOneEntry)
{
    MemoryHarness harness;
    const EntityId eye = harness.RememberingObserver(Vec3d::Zero());
    const GameplayTagId sense = *harness.Entities.GetResource<GameplayTagRegistry>().RegisterTag("test.sense");
    harness.Tick();

    PerceptionFrame& frame = harness.Entities.GetResource<PerceptionFrame>();
    const auto hear = [&](const Vec3d& position) {
        frame.Begin(100);
        frame.BeginScan(eye, sense);
        frame.Add(EntityId{}, position, 0.5f);
        frame.EndScan(ScanCoverage::Complete);
        frame.Publish();
        FixedLogicContext ctx{
            .Config = harness.Config,
            .Runtime = harness.Runtime,
            .Time = FixedSimTime{ .DeltaSeconds = kDt, .TickIndex = 100 },
            .Entities = harness.Entities,
            .Partitions = harness.Partitions,
        };
        harness.Memory.FixedLogic(ctx);
    };
    hear(Vec3d(10, 0, 0));
    hear(Vec3d(11, 0, 0));
    hear(Vec3d(-10, 0, 0));

    const ObservationMemory& memory = *harness.Entities.TryGet<ObservationMemory>(eye);
    ASSERT_EQ(memory.Count, 2u);
    EXPECT_EQ(memory.Entries[0].Position, Vec3d(11, 0, 0));
    EXPECT_FALSE(memory.Entries[0].Subject.IsValid());
    EXPECT_EQ(memory.Entries[1].Position, Vec3d(-10, 0, 0));
}

TEST(ObservationMemory, QueriesAnswerFromMemory)
{
    MemoryHarness harness;
    const EntityId eye = harness.RememberingObserver(Vec3d::Zero());
    const EntityId target = harness.Target(Vec3d(0, 0, -5));
    const EntityId stranger = harness.Target(Vec3d(0, 0, 5));
    harness.Ticks(3);
    harness.Place(target, Vec3d(0, 0, 5));
    harness.Ticks(6);

    AuthoredValue answer;
    bool remembered = false;
    ASSERT_EQ(harness.Ask(kRemembersQuery, eye, target, answer), AuthoredQueryStatus::Value);
    ASSERT_TRUE(answer.TryGetBool(remembered));
    EXPECT_TRUE(remembered);
    ASSERT_EQ(harness.Ask(kRemembersQuery, eye, stranger, answer), AuthoredQueryStatus::Value);
    ASSERT_TRUE(answer.TryGetBool(remembered));
    EXPECT_FALSE(remembered);

    AuthoredVectorValue position;
    ASSERT_EQ(harness.Ask(kLastSeenPositionQuery, eye, target, answer), AuthoredQueryStatus::Value);
    ASSERT_TRUE(answer.TryGetVector(position));
    EXPECT_DOUBLE_EQ(position.Components[2], -5.0);
    EXPECT_EQ(harness.Ask(kLastSeenPositionQuery, eye, stranger, answer), AuthoredQueryStatus::Unavailable);

    double seconds = 0.0;
    ASSERT_EQ(harness.Ask(kSecondsSinceSeenQuery, eye, target, answer), AuthoredQueryStatus::Value);
    ASSERT_TRUE(answer.TryGetFloat(seconds));
    EXPECT_NEAR(seconds, 6 * kDt, 1e-4);
    ASSERT_EQ(harness.Ask(kSecondsTrackedQuery, eye, target, answer), AuthoredQueryStatus::Value);
    ASSERT_TRUE(answer.TryGetFloat(seconds));
    EXPECT_NEAR(seconds, 2 * kDt, 1e-4);
}

TEST(ObservationMemory, NearestRememberedTaggedSkipsTheDeadAndTheUntagged)
{
    MemoryHarness harness;
    GameplayTagRegistry& tags = harness.Entities.GetResource<GameplayTagRegistry>();
    const GameplayTagId hostile = *tags.RegisterTag("faction.hostile");
    const EntityId eye = harness.RememberingObserver(Vec3d::Zero());
    const EntityId near = harness.Target(Vec3d(0, 0, -3));
    const EntityId far = harness.Target(Vec3d(0, 0, -9));
    const EntityId untagged = harness.Target(Vec3d(0, 0, -1));
    for (const EntityId entity : { near, far })
    {
        GameplayTagContainer container;
        (void)container.Grant(hostile);
        harness.Entities.AddComponent<GameplayTagContainer>(entity, container);
    }
    harness.Tick();

    const auto nearest = [&]() {
        AuthoredValue answer;
        const std::vector<AuthoredValue> arguments{ AuthoredValue::Entity(eye), AuthoredValue::Tag(hostile) };
        const AuthoredQueryStatus status = harness.Dispatcher->Evaluate(
            harness.Dispatcher->Registry().Resolve(kNearestRememberedTaggedQuery), arguments, answer);
        EntityId entity;
        return status == AuthoredQueryStatus::Value && answer.TryGetEntity(entity) ? entity : EntityId{};
    };
    EXPECT_EQ(nearest(), near);
    harness.Entities.DestroyEntity(near);
    EXPECT_EQ(nearest(), far);
    (void)untagged;
}

TEST(ObservationMemory, AClientRemembersNothingAndAnswersUnavailable)
{
    MemoryHarness harness;
    harness.Entities.AddResource<SimulationAuthority>(SimulationAuthority{ .Authoritative = false });
    const EntityId eye = harness.RememberingObserver(Vec3d::Zero());
    const EntityId target = harness.Target(Vec3d(0, 0, -5));
    harness.Ticks(3);

    EXPECT_EQ(harness.Entities.TryGet<ObservationMemory>(eye)->Count, 0u);
    AuthoredValue answer;
    EXPECT_EQ(harness.Ask(kRemembersQuery, eye, target, answer), AuthoredQueryStatus::Unavailable);
}
