// An actor's time: the scale its leases put in force on each tick, how that clock
// advances, and what runs on it.

#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <ecs/World.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <time/ActorTime.h>
#include <time/ActorTimeLeases.h>
#include <time/ActorTimeSystem.h>
#include <time/ActorTimeVerbs.h>
#include <time/SimClock.h>
#include <time/TimeComponentRegistration.h>
#include <world/ComponentRegistrar.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

namespace
{
std::int64_t Q16(double scale)
{
    return std::llround(scale * static_cast<double>(kActorTimeOne));
}

LeaseOwner Holder(std::uint64_t key)
{
    return LeaseOwner{ .Entity = EntityId{ 1, 1 }, .Key = key };
}

struct TimeWorld
{
    World Entities;
    EntityId Actor;
    std::uint64_t Now = 0;

    TimeWorld()
    {
        ComponentRegistrar registrar(Entities);
        RegisterTimeComponents(registrar);
        Entities.RegisterComponent<AttributeSet>();
        Entities.RegisterComponent<GameplayTagContainer>();
        Entities.RegisterComponent<ActiveEffect>();
        Entities.AddResource<AttributeRegistry>();
        Entities.AddResource<EffectRegistry>();
        Actor = Entities.CreateEntity();
        Entities.AddComponent<ActorTime>(Actor, ActorTime{});
        Entities.AddComponent<ActorTimeLeases>(Actor, ActorTimeLeases{});
    }

    ActorTimeLeases& Leases() { return *Entities.TryGet<ActorTimeLeases>(Actor); }
    const ActorTime& Clock() { return *Entities.TryGet<ActorTime>(Actor); }

    // A fixed tick in the schedule's order: the clock first, effects last.
    void Step(int ticks = 1)
    {
        for (int i = 0; i < ticks; ++i)
        {
            AdvanceActorTime(Entities, Now);
            ResolveAttributesWithEffects(Entities);
            TickEffects(Entities);
            ++Now;
        }
    }
};
}

TEST(ActorTime, TheScaleIsTheProductOfTheLeasesInForce)
{
    ActorTimeLeases leases;
    ASSERT_TRUE(AcquireActorTimeLease(leases, Holder(1), Q16(0.5), 0));
    ASSERT_TRUE(AcquireActorTimeLease(leases, Holder(2), Q16(1.5), 0));
    EXPECT_EQ(ActorScaleAt(leases, 1, {}), Q16(0.75));
}

TEST(ActorTime, TheScaleIsIdenticalWhateverOrderTheLeasesWereTakenIn)
{
    std::array<double, 4> scales{ 0.3, 1.7, 0.9, 1.13 };
    std::sort(scales.begin(), scales.end());
    std::int64_t first = -1;
    do
    {
        ActorTimeLeases leases;
        for (std::size_t i = 0; i < scales.size(); ++i)
            ASSERT_TRUE(AcquireActorTimeLease(leases, Holder(i), Q16(scales[i]), 0));
        const std::int64_t scale = ActorScaleAt(leases, 1, {});
        if (first < 0)
            first = scale;
        EXPECT_EQ(scale, first);
    } while (std::next_permutation(scales.begin(), scales.end()));
}

TEST(ActorTime, ALeaseHoldsFromTheTickAfterItIsTakenThroughTheTickItIsLetGo)
{
    ActorTimeLeases leases;
    ASSERT_TRUE(AcquireActorTimeLease(leases, Holder(1), Q16(0.5), 5));
    EXPECT_EQ(ActorScaleAt(leases, 5, {}), kActorTimeOne);
    EXPECT_EQ(ActorScaleAt(leases, 6, {}), Q16(0.5));
    ReleaseActorTimeLeases(leases, Holder(1), 9);
    EXPECT_EQ(ActorScaleAt(leases, 9, {}), Q16(0.5));
    EXPECT_EQ(ActorScaleAt(leases, 10, {}), kActorTimeOne) << "exactly one again, not a rounding of it";
    EXPECT_EQ(ActorScaleAt(leases, 7, {}), Q16(0.5)) << "the ended lease stays history for a replayed tick";
}

TEST(ActorTime, TheScaleIsClampedToItsLimits)
{
    ActorTimeLeases leases;
    ASSERT_TRUE(AcquireActorTimeLease(leases, Holder(1), Q16(3.0), 0));
    ASSERT_TRUE(AcquireActorTimeLease(leases, Holder(2), Q16(3.0), 0));
    EXPECT_EQ(ActorScaleAt(leases, 1, {}), Q16(2.0));
    EXPECT_EQ(ActorScaleAt(leases, 1, { .MinimumQ16 = Q16(0.1), .MaximumQ16 = Q16(16.0) }), kActorScaleHardMaximumQ16);
    ActorTimeLeases slowed;
    ASSERT_TRUE(AcquireActorTimeLease(slowed, Holder(1), Q16(0.01), 0));
    EXPECT_EQ(ActorScaleAt(slowed, 1, {}), Q16(0.1));
}

TEST(ActorTime, HistoryNeverUsesTheRoomANewLeaseNeeds)
{
    ActorTimeLeases leases;
    for (std::uint64_t tick = 0; tick < 40; ++tick)
    {
        ASSERT_TRUE(AcquireActorTimeLease(leases, Holder(tick), Q16(0.5), tick)) << "tick " << tick;
        ReleaseActorTimeLeases(leases, Holder(tick), tick + 1);
    }
    EXPECT_EQ(leases.ActiveCount, 0u);
    EXPECT_EQ(leases.HistoryCount, kActorTimeHistoryCapacity);
    EXPECT_EQ(leases.Evicted, 40u - kActorTimeHistoryCapacity) << "the oldest went, and were counted";
    for (std::size_t i = 0; i < kActorTimeLeaseCapacity; ++i)
        EXPECT_TRUE(AcquireActorTimeLease(leases, Holder(100 + i), Q16(0.9), 50));
}

TEST(ActorTime, HistoryLeavesOnceItsWindowCloses)
{
    ActorTimeLeases leases;
    ASSERT_TRUE(AcquireActorTimeLease(leases, Holder(1), Q16(0.5), 0));
    ReleaseActorTimeLeases(leases, Holder(1), 3);
    PruneActorTimeHistory(leases, 4 + kActorTimeHistoryTicks - 1);
    EXPECT_EQ(leases.HistoryCount, 1u);
    PruneActorTimeHistory(leases, 4 + kActorTimeHistoryTicks);
    EXPECT_EQ(leases.HistoryCount, 0u);
}

TEST(ActorTime, DilationSlowsTheClockAndNeverTheCadence)
{
    TimeWorld time;
    ASSERT_TRUE(AcquireActorTimeLease(time.Leases(), LeaseOwner{ .Entity = time.Actor }, Q16(0.5), 0));
    time.Step(11);
    EXPECT_EQ(ActorTicks(time.Clock()), 6u) << "tick zero at full speed, then ten at half";
    EXPECT_EQ(time.Clock().ScaleQ16, Q16(0.5));
}

TEST(ActorTime, ALeaseWhoseHolderIsGoneEndsTheTickAfter)
{
    TimeWorld time;
    const EntityId holder = time.Entities.CreateEntity();
    ASSERT_TRUE(AcquireActorTimeLease(time.Leases(), LeaseOwner{ .Entity = holder }, Q16(0.5), 0));
    time.Step(3);
    time.Entities.DestroyEntity(holder);
    time.Step();
    EXPECT_EQ(time.Clock().ScaleQ16, kActorTimeOne);
    EXPECT_EQ(time.Leases().ActiveCount, 0u);
    EXPECT_EQ(time.Leases().HistoryCount, 1u);
}

TEST(ActorTime, AnEffectsDurationRunsInItsTargetsTime)
{
    TimeWorld time;
    EffectDefinition brief{};
    brief.Duration = EffectDuration::Duration;
    brief.DurationSeconds = 10.0f;
    EffectDefinition steady = brief;
    steady.TimeDomain = EffectTimeDomain::Simulation;
    EffectRegistry& effects = time.Entities.GetResource<EffectRegistry>();
    const EffectId targetTime = effects.Define("target", brief);
    const EffectId simulationTime = effects.Define("simulation", steady);
    ASSERT_TRUE(AcquireActorTimeLease(time.Leases(), LeaseOwner{ .Entity = time.Actor }, Q16(0.5), 0));
    time.Step();

    const FixedSimTime now{ .DeltaSeconds = 1.0, .TickIndex = time.Now };
    const EntityId slowed = ApplyEffect(time.Entities, time.Actor, targetTime, now);
    const EntityId unaffected = ApplyEffect(time.Entities, time.Actor, simulationTime, now);
    time.Step(10);
    EXPECT_FALSE(time.Entities.IsAlive(unaffected));
    EXPECT_TRUE(time.Entities.IsAlive(slowed));
    time.Step(10);
    EXPECT_FALSE(time.Entities.IsAlive(slowed)) << "ten of its target's ticks are twenty of the simulation's";
}

TEST(ActorTime, AnEffectWithATimeScaleHoldsItForItsLifetime)
{
    TimeWorld time;
    EffectDefinition haste{};
    haste.Duration = EffectDuration::Infinite;
    haste.TimeScale = 1.5f;
    const EffectId hasted = time.Entities.GetResource<EffectRegistry>().Define("haste", haste);
    const EntityId held =
        ApplyEffect(time.Entities, time.Actor, hasted, FixedSimTime{ .DeltaSeconds = 1.0, .TickIndex = time.Now });
    time.Step(2);
    EXPECT_EQ(time.Clock().ScaleQ16, Q16(1.5));
    ASSERT_TRUE(RemoveEffect(time.Entities, held));
    time.Step();
    EXPECT_EQ(time.Clock().ScaleQ16, kActorTimeOne);
}

TEST(ActorTime, TheTimeScaleLeaseIsTakenAndLetGo)
{
    TimeWorld time;
    VerbRegistry& verbs = InstallVerbRegistry(time.Entities);
    {
        VerbRegistrationScope scope(verbs, "engine");
        DeclareActorTimeVerbs(scope);
        ASSERT_TRUE(scope.Commit());
    }
    FixedSimulationLoop clock;
    ActorTimeOperations operations(time.Entities, clock);
    VerbDispatcher dispatcher(verbs);
    const VerbBindingToken token = dispatcher.Bind<&InvokeTimeScale>(verbs.Find(kTimeScaleLease), operations);

    VerbBindingDesc desc;
    desc.Key = "slow";
    desc.KeyId = MakeVerbBindingKey(desc.Key);
    desc.VerbName = std::string(kTimeScaleLease);
    desc.Inputs = { "target" };
    VerbBindingArgument target;
    target.Key = "target";
    target.Source = VerbArgumentSource::Input;
    target.Text = "target";
    VerbBindingArgument scale;
    scale.Key = "scale";
    scale.Literal = JsonValue(0.25);
    desc.Arguments = { target, scale };
    CompiledVerbBinding binding;
    std::vector<std::string> errors;
    ASSERT_TRUE(CompileVerbBinding(desc, VerbBindingEnvironment{ .Verbs = &verbs }, binding, errors)) << errors.front();

    const AuthoredValue subject = AuthoredValue::Entity(time.Actor);
    const LeaseOwner owner{ .Entity = time.Actor, .Key = 7 };
    ASSERT_TRUE(dispatcher.AcquireLease(binding, std::span(&subject, 1), owner, { .Tick = time.Now }).Accepted());
    time.Step(2);
    EXPECT_EQ(time.Clock().ScaleQ16, Q16(0.25));
    EXPECT_EQ(dispatcher.ReleaseLease(verbs.Find(kTimeScaleLease), owner, { .Tick = time.Now }),
              VerbAdmission::Accepted);
    time.Step(2);
    EXPECT_EQ(time.Clock().ScaleQ16, kActorTimeOne);
}
