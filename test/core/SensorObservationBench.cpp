#include "AllocationCounter.h"
#include "BenchRecorder.h"

#include <app/GameContexts.h>
#include <movement/components/CharacterMovement.h>
#include <physics/CharacterControllerSystem.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/SensorOverlapSystem.h>
#include <physics/SensorOverlaps.h>
#include <physics/components/CharacterController.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/Collider.h>
#include <physics/components/RigidBody.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <format>

namespace
{
struct Scenario
{
    const char* Name;
    std::uint32_t Sensors;
    std::uint32_t Bodies;
    std::uint32_t Characters = 0;
    BodyMotion Motion = BodyMotion::Dynamic;
    bool Moving = false;
    bool Dense = false;
    std::uint32_t Solids = 0;
};

class Fixture
{
public:
    explicit Fixture(const Scenario& scenario, double dt)
        : Case(scenario), Delta(dt), Characters(Physics), Observations(Physics)
    {
        Entities.RegisterComponent<LocalTransform>();
        Entities.RegisterComponent<WorldTransform>();
        Entities.RegisterComponent<Parent>();
        RegisterPhysicsComponents(Entities);
        Entities.RegisterComponent<MotionRequest>();
        Entities.RegisterComponent<KinematicState>();
        Entities.RegisterComponent<SupportState>();
        Entities.RegisterComponent<ActorTimeDilated>();
        Entities.AddResource<SensorOverlaps>(32768);
        Partitions.Add(StoragePartitionId::Default());
        if (Case.Solids != 0)
            for (std::uint32_t room = 0; room < 32; ++room)
                Partitions.Add(StoragePartitionId{ static_cast<std::uint16_t>(room + 1) });
        for (std::uint32_t i = 0; i < Case.Sensors; ++i)
        {
            auto entity = Spawn(Case.Dense ? Vec3d::Zero() :
                Case.Solids != 0 ? RoomCenter(i) : Site(i), i);
            Entities.AddComponent<Collider>(entity, Collider{
                .Shape = CollisionShape::MakeBox(Case.Dense ? Vec3d(64, 4, 64) :
                    Case.Solids != 0 ? Vec3d(2, 2, 2) : Vec3d(1, 2, 1)),
                .IsTrigger = true });
        }
        for (std::uint32_t i = 0; i < Case.Bodies + Case.Characters; ++i)
        {
            const auto room = i < Case.Bodies ? i / 8 : i - Case.Bodies;
            const auto position = Case.Solids != 0 && i >= Case.Bodies
                ? RoomCenter(room) : Site(i);
            const auto entity = Spawn(position, room);
            if (i < Case.Bodies)
            {
                Entities.AddComponent<Collider>(entity, Collider{ .Shape = CollisionShape::MakeSphere(0.25f) });
                Entities.AddComponent<RigidBody>(entity, RigidBody{
                    .Motion = Case.Motion, .GravityScale = 0, .LinearDamping = 0 });
                Bodies.push_back(entity);
            }
            else
            {
                Entities.AddComponent<CharacterController>(entity, CharacterController{});
                Entities.AddComponent<MotionRequest>(entity, MotionRequest{ .GravityScale = 0 });
                Entities.AddComponent<KinematicState>(entity, KinematicState{});
                Entities.AddComponent<SupportState>(entity, SupportState{});
                Movers.push_back(entity);
            }
        }
        for (std::uint32_t i = 0; i < Case.Solids; ++i)
        {
            const auto part = i % 32;
            Vec3d offset;
            Vec3d extents;
            if (part == 0)
            {
                offset = Vec3d(0, -1.25f, 0);
                extents = Vec3d(7.75f, 0.25f, 7.75f);
            }
            else if (part <= 4)
            {
                const float side = part % 2 == 0 ? -8.0f : 8.0f;
                offset = part <= 2 ? Vec3d(side, 1, 0) : Vec3d(0, 1, side);
                extents = part <= 2 ? Vec3d(0.25f, 2, 8) : Vec3d(8, 2, 0.25f);
            }
            else
            {
                offset = Vec3d(static_cast<float>(part % 7) * 2 - 6, 0,
                               part % 2 == 0 ? -6.0f : 6.0f);
                extents = Vec3d(0.25f, 0.5f, 0.25f);
            }
            const auto entity = Spawn(RoomCenter(i / 32) + offset, i / 32);
            Entities.AddComponent<Collider>(entity, Collider{ .Shape = CollisionShape::MakeBox(extents) });
        }
    }

    void Advance()
    {
        ++Tick;
        Entities.AdvanceFrame();
        const float speed = Case.Moving ? ((Tick / 30) % 2 == 0 ? 3.0f : -3.0f) : 0.0f;
        for (auto entity : Bodies)
        {
            if (!Case.Moving)
                continue;
            if (Case.Motion == BodyMotion::Kinematic)
                Entities.TryGet<LocalTransform>(entity)->Value.Position.X += speed * static_cast<float>(Delta);
            else if (const auto* link = std::as_const(Entities).TryGet<PhysicsBodyLink>(entity))
                Physics.GetSimulation().SetLinearVelocity(link->Body, Vec3d(speed, 0, 0));
        }
        for (auto entity : Movers)
            Entities.TryGet<MotionRequest>(entity)->Velocity = Vec3d(speed, 0, 0);
        PhysicsContext context{ Config, Runtime, { Delta, Tick }, Entities, Partitions };
        const auto before = AllocationCount();
        const auto start = Bench::Clock::now();
        Physics.Physics(context);
        Characters.Physics(context);
        SyncMs = Bench::MillisecondsSince(start);
        SyncAllocations = AllocationCount() - before;
    }

    double Observe()
    {
        PhysicsContext context{ Config, Runtime, { Delta, Tick }, Entities, Partitions };
        const auto start = Bench::Clock::now();
        Observations.Physics(context);
        return Bench::MillisecondsSince(start);
    }

    void Attribute(double& bodies, double& characters, double& tracking)
    {
        auto& observations = Entities.GetResource<SensorOverlaps>();
        observations.Begin();
        auto start = Bench::Clock::now();
        Physics.GetSimulation().ObserveSensors(observations);
        bodies = Bench::MillisecondsSince(start);
        start = Bench::Clock::now();
        Physics.GetCharacterMovers().ObserveSensors(observations);
        characters = Bench::MillisecondsSince(start);
        start = Bench::Clock::now();
        EXPECT_TRUE(observations.Commit(Tick));
        tracking = Bench::MillisecondsSince(start);
    }

    const SensorOverlaps& Sample() const { return *Entities.TryGetResource<SensorOverlaps>(); }
    std::size_t Sleeping() const
    {
        std::size_t count = 0;
        for (auto entity : Bodies)
            if (const auto* link = Entities.TryGet<PhysicsBodyLink>(entity))
                count += !Physics.GetSimulation().IsBodyActive(link->Body);
        return count;
    }

    double SyncMs = 0;
    std::size_t SyncAllocations = 0;

private:
    static Vec3d RoomCenter(std::uint32_t room)
    {
        return Vec3d(static_cast<float>(room % 8) * 16, 0, static_cast<float>(room / 8) * 16);
    }
    Vec3d Site(std::uint32_t i) const
    {
        if (Case.Solids != 0)
            return RoomCenter(i / 8) + Vec3d(static_cast<float>(i % 4) * 2 - 3, 0,
                                           (i / 4) % 2 == 0 ? -2.0f : 2.0f);
        return Vec3d(static_cast<float>(i % 32) * 3, 0, static_cast<float>(i / 32) * 3);
    }
    EntityId Spawn(Vec3d position, std::uint32_t room = 0)
    {
        const auto partition = Case.Solids != 0 ? StoragePartitionId{ static_cast<std::uint16_t>(room + 1) } : StoragePartitionId::Default();
        const auto entity = Entities.CreateEntity(partition);
        LocalTransform transform;
        transform.Value.Position = position;
        Entities.AddComponent<LocalTransform>(entity, transform);
        Entities.AddComponent<WorldTransform>(entity, WorldTransform{});
        return entity;
    }

    Scenario Case;
    double Delta;
    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    World Entities;
    PhysicsStepSystem Physics;
    CharacterControllerSystem Characters;
    SensorOverlapSystem Observations;
    StoragePartitionSet Partitions;
    std::vector<EntityId> Bodies;
    std::vector<EntityId> Movers;
    std::uint64_t Tick = 0;
};
}

TEST(SensorObservationBench, Generate)
{
    const char* path = std::getenv("SENCHA_SENSOR_BENCH_OUT");
    if (path == nullptr || path[0] == '\0')
        GTEST_SKIP() << "Set SENCHA_SENSOR_BENCH_OUT to record physics observation costs";
    const int reps = Bench::RepsFromEnvironment("SENCHA_SENSOR_BENCH_REPS", 600);
    Bench::Recorder recorder;
    const Scenario scenarios[]{
        { "no_sensors", 0, 2048 },
        { "no_sensors_moving", 0, 2048, 0, BodyMotion::Dynamic, true },
        { "no_sensors_characters", 0, 0, 128, BodyMotion::Dynamic, true },
        { "few_sleeping", 4, 2048 },
        { "few_stationary_kinematic", 4, 2048, 0, BodyMotion::Kinematic },
        { "few_dynamic", 4, 2048, 0, BodyMotion::Dynamic, true },
        { "few_kinematic", 4, 2048, 0, BodyMotion::Kinematic, true },
        { "many_few", 512, 32 },
        { "many_many_sparse", 256, 1024, 0, BodyMotion::Dynamic, true },
        { "dense_sleeping", 64, 128, 0, BodyMotion::Dynamic, false, true },
        { "dense_moving", 64, 128, 0, BodyMotion::Kinematic, true, true },
        { "characters", 256, 0, 128, BodyMotion::Dynamic, true },
        { "rooms", 32, 256, 16, BodyMotion::Dynamic, true, false, 1024 },
    };
    for (int hz : { 60, 120 })
    for (const auto& scenario : scenarios)
    {
        const char* filter = std::getenv("SENCHA_SENSOR_BENCH_CASE");
        if (filter != nullptr && std::string_view(filter) != scenario.Name)
            continue;
        Fixture fixture(scenario, 1.0 / hz);
        for (int i = 0; i < 180; ++i)
        {
            fixture.Advance();
            (void)fixture.Observe();
        }
        std::vector<double> total(reps), body(reps), character(reps), tracking(reps), sync(reps);
        std::size_t allocations = 0, syncAllocations = 0;
        std::uint64_t overlapQueries = 0, casts = 0;
        for (int i = 0; i < reps; ++i)
        {
            fixture.Advance();
            sync[i] = fixture.SyncMs;
            syncAllocations += fixture.SyncAllocations;
            auto before = AllocationCount();
            total[i] = fixture.Observe();
            allocations += AllocationCount() - before;
            overlapQueries += fixture.Sample().QueryCounts().Overlaps;
            casts += fixture.Sample().QueryCounts().Casts;
            EXPECT_EQ(fixture.Sample().OverflowCount(), 0u);
            if (scenario.Moving && scenario.Sensors != 0)
            {
                EXPECT_EQ(fixture.Sample().QueryCounts().Casts, scenario.Bodies + scenario.Characters);
            }
            fixture.Advance();
            fixture.Attribute(body[i], character[i], tracking[i]);
        }
        const auto prefix = std::format("sensor.{}.{}hz.", scenario.Name, hz);
        recorder.Record(prefix + "observation_median", "ms", Bench::Median(total));
        recorder.Record(prefix + "observation_p95", "ms", Bench::Percentile(total, 0.95));
        recorder.Record(prefix + "observation_worst", "ms", *std::max_element(total.begin(), total.end()));
        recorder.Record(prefix + "body_queries_median", "ms", Bench::Median(body));
        recorder.Record(prefix + "character_queries_median", "ms", Bench::Median(character));
        recorder.Record(prefix + "tracking_median", "ms", Bench::Median(tracking));
        recorder.Record(prefix + "physics_and_sync_median", "ms", Bench::Median(sync));
        recorder.Record(prefix + "overlap_queries_per_tick", "count", static_cast<double>(overlapQueries) / reps);
        recorder.Record(prefix + "casts_per_tick", "count", static_cast<double>(casts) / reps);
        recorder.Record(prefix + "physics_and_sync_allocations", "count", static_cast<double>(syncAllocations));
        recorder.Record(prefix + "allocations", "count", static_cast<double>(allocations));
        recorder.Record(prefix + "sleeping_bodies", "count", static_cast<double>(fixture.Sleeping()));
        recorder.Record(prefix + "pairs", "count", static_cast<double>(fixture.Sample().Pairs().size()));
        EXPECT_EQ(allocations, 0u) << prefix;
    }
    EXPECT_TRUE(recorder.WriteJson(path));
}
