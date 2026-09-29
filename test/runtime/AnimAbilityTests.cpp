// An ability asks its actor for animation through the door every producer uses,
// and owns what it asks for: a Held request lasts exactly as long as the
// activation that asked.

#include "AnimFlowFixture.h"

#include <abilities/AbilityActivationSystem.h>
#include <abilities/AbilityKit.h>
#include <anim/AnimRequestJournal.h>
#include <anim/AnimationRegistration.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <core/config/EngineConfig.h>
#include <ecs/StoragePartitionSet.h>
#include <runtime/RuntimeFrameLoop.h>

#include <gtest/gtest.h>

namespace
{
    // The reload rig on an actor that has the Reload ability, whose activation
    // lasts half a second, and the Flick ability, which is instant.
    struct AbilityFixture : AnimRigFixture
    {
        DataAssetHandle Rig;
        EntityId Actor;
        AbilityId Reload;
        AbilityId Flick;

        AbilityFixture()
        {
            RegisterAbilityKit(Entities);
            Rig = LoadReloadRig(*this, CountedLoopFlow());

            EffectDefinition active;
            active.Duration = EffectDuration::Duration;
            active.DurationSeconds = 0.5f;
            const EffectId reloading = Entities.GetResource<EffectRegistry>().Register("Reloading", active);

            AbilityDefinition reload;
            reload.OnActivate = reloading;
            reload.Animation.Intent = Tag("anim.intent.reload");
            Reload = Entities.GetResource<AbilityRegistry>().Register("Reload", reload);

            AbilityDefinition flick;
            flick.Animation.Intent = Tag("anim.intent.reload");
            Flick = Entities.GetResource<AbilityRegistry>().Register("Flick", flick);

            Actor = Character(Rig);
            Entities.AddComponent<AbilitySet>(Actor);
            Entities.TryGet<AbilitySet>(Actor)->Grant(Reload);
            Entities.TryGet<AbilitySet>(Actor)->Grant(Flick);
        }

        const AnimRequest* Asked()
        {
            for (const AnimRequest& request : static_cast<const World&>(Entities).TryGet<AnimRequestSet>(Actor)->Records)
                if (request.Occupied && request.Intent == Tag("anim.intent.reload"))
                    return &request;
            return nullptr;
        }

        // What the schedule runs on a tick, in its order: the kit releases ended
        // activations and activates, animation plays, effects age.
        void TickWithTheKit()
        {
            ReleaseEndedAbilityAnimations(Entities, Now);
            Tick();
            TickEffects(Entities, static_cast<float>(kTick));
        }
    };
}

TEST(AnimAbility, AnActivationHoldsItsAnimationForAsLongAsItLasts)
{
    AbilityFixture fx;
    fx.Tick();
    const AnimTick activated = fx.Now;
    ASSERT_TRUE(TryActivateAbility(fx.Entities, fx.Actor, fx.Reload, activated));
    const AnimRequest* asked = fx.Asked();
    ASSERT_NE(asked, nullptr);
    EXPECT_EQ(asked->Id.Source, fx.Actor);
    EXPECT_EQ(asked->Lifetime, AnimRequestLifetime::Held);
    EXPECT_TRUE(asked->Owner.IsValid()) << "owned by the activation";
    fx.TickWithTheKit();
    EXPECT_EQ(fx.BehaviorName(fx.Actor), "anim.intent.reload");

    // Half a second is thirty ticks; the activation ends and the kit lets go.
    for (int i = 0; i < 35; ++i)
        fx.TickWithTheKit();
    asked = fx.Asked();
    ASSERT_NE(asked, nullptr) << "kept while its cancel section plays out";
    EXPECT_EQ(asked->CancelReason, AnimCancelReason::Released);
    EXPECT_GE(asked->CancelTick, activated + 30);
    EXPECT_LE(asked->CancelTick, activated + 32);
    EXPECT_EQ(fx.Entities.TryGet<AnimRequestReport>(fx.Actor)->Orphaned, 0u);
}

// An activation that lasts no time holds its animation for the tick it happened.
TEST(AnimAbility, AnInstantActivationHoldsItsAnimationForItsTick)
{
    AbilityFixture fx;
    fx.Tick();
    ASSERT_TRUE(TryActivateAbility(fx.Entities, fx.Actor, fx.Flick, fx.Now));
    const AnimRequest* asked = fx.Asked();
    ASSERT_NE(asked, nullptr);
    EXPECT_EQ(asked->Lifetime, AnimRequestLifetime::Impulse);
    EXPECT_FALSE(asked->Owner.IsValid());
}

// Through the schedule: an activation queued before a tick is asked for, and
// played, on that same tick.
TEST(AnimAbility, AnActivationIsPlayedOnItsOwnTick)
{
    AbilityFixture fx;
    EngineConfig config;
    RuntimeFrameLoop runtime;
    EngineSchedule schedule;
    RegisterAnimationSystems(schedule, nullptr, AnimationHost{ .PresentsPose = false });
    RegisterAbilityKitSystems(schedule);
    schedule.Init();
    StoragePartitionSet partitions;
    partitions.Add(StoragePartitionId::Default());
    const auto run = [&](std::uint64_t tick) {
        FixedLogicContext context{
            .Config = config,
            .Runtime = runtime,
            .Time = FixedSimTime{ .DeltaSeconds = AnimRigFixture::kTick, .TickIndex = tick },
            .Entities = fx.Entities,
            .Partitions = partitions,
        };
        schedule.RunFixedLogic(context);
    };
    for (std::uint64_t tick = 0; tick < 5; ++tick)
        run(tick);
    fx.Entities.GetResource<AbilityActivationQueue>().Pending.push_back({ fx.Actor, fx.Reload });
    run(5);
    EXPECT_EQ(fx.Playing(fx.Actor).StartTick, 5u);
    EXPECT_EQ(fx.BehaviorName(fx.Actor), "anim.intent.reload");
}

// A producer that ends without cancelling what it holds is a bug animation makes
// visible, not one it tidies away: the request stays held, and is counted.
TEST(AnimAbility, AHeldRequestItsProducerForgotIsReportedNotEnded)
{
    AbilityFixture fx;
    const EntityId owner = fx.Entities.CreateEntity();
    fx.Tick();
    AnimRequestDesc desc;
    desc.Source = fx.Actor;
    desc.Intent = fx.Tag("anim.intent.reload");
    desc.Owner = owner;
    ASSERT_TRUE(RequestAnimation(fx.Entities, fx.Actor, desc, fx.Now).Accepted());
    fx.Tick(3);
    EXPECT_EQ(fx.Entities.TryGet<AnimRequestReport>(fx.Actor)->Orphaned, 0u);

    fx.Entities.DestroyEntity(owner);
    fx.Tick();
    EXPECT_EQ(fx.Entities.TryGet<AnimRequestReport>(fx.Actor)->Orphaned, 0u)
        << "one pass for the producer to let go";
    fx.Tick(3);
    EXPECT_EQ(fx.Entities.TryGet<AnimRequestReport>(fx.Actor)->Orphaned, 1u) << "counted once";
    const AnimRequest* held = fx.Asked();
    ASSERT_NE(held, nullptr);
    EXPECT_FALSE(held->IsCancelled()) << "the producer's to end";
    EXPECT_NE(fx.LastRecord(fx.Actor, AnimDecisionCause::RequestOrphaned), nullptr);
}
