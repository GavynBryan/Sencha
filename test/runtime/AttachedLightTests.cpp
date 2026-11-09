#include <gtest/gtest.h>

#include "AttachmentFixture.h"

#include <attach/AttachedLight.h>
#include <attach/AttachmentVerbs.h>
#include <authored/AuthoredValue.h>
#include <time/SimClock.h>

#include <cmath>
#include <vector>

namespace
{
    using attach_test::AttachFixture;

    constexpr std::uint64_t kStart = 100;
    constexpr double kTick = 1.0 / 60.0;

    struct LightFixture : AttachFixture
    {
        AttachedLightState& State = Entities.AddResource<AttachedLightState>();
        LeaseOwner Holder{ .Entity = Entities.CreateEntity(), .Key = 1 };

        [[nodiscard]] AttachedLightSpec Spec(float attack = 0.0f, float release = 0.0f) const
        {
            AttachedLightSpec spec;
            spec.Attachment.Root = Body;
            spec.Attachment.Light.Intensity = 4.0f;
            spec.Attachment.Light.Range = 6.0f;
            spec.Envelope = { .Attack = attack, .Hold = 0.0f, .Release = release };
            spec.PeakScale = 3.0f;
            spec.PulseSeconds = 0.1f;
            return spec;
        }

        [[nodiscard]] EntityId Worn() const
        {
            EntityId worn;
            ForEachAttachment(Entities, Body, [&](EntityId entity, const Attachment&) { worn = entity; });
            return worn;
        }

        float At(double seconds)
        {
            const double ticks = seconds / kTick;
            const double whole = std::floor(ticks);
            State.Update(Entities, { .Tick = kStart + 1 + static_cast<std::uint64_t>(whole),
                                     .Alpha = ticks - whole,
                                     .TickSeconds = kTick });
            const EntityId worn = Worn();
            return worn.IsValid() ? Entities.TryGet<PointLightComponent>(worn)->Intensity : -1.0f;
        }
    };

    struct LightVerbFixture : LightFixture
    {
        FixedSimulationLoop Clock{ kTick };
        AttachedLightOperations Operations{ Entities, Clock };

        [[nodiscard]] AuthoredArguments Arguments(double intensity = 4.0) const
        {
            AuthoredArguments arguments;
            arguments.Resize(11);
            arguments.Set(0, AuthoredValue::Entity(Body));
            arguments.Set(3, AuthoredValue::Vector({ .Components = { 0.1, 0.3, 1.0, 0.0 }, .Length = 3 }));
            arguments.Set(4, AuthoredValue::Float(intensity));
            arguments.Set(5, AuthoredValue::Float(6.0));
            arguments.Set(7, AuthoredValue::Float(0.1));
            arguments.Set(8, AuthoredValue::Float(0.0));
            arguments.Set(9, AuthoredValue::Float(0.2));
            arguments.Set(10, AuthoredValue::Bool(true));
            return arguments;
        }

        VerbAdmission Step(LeaseStep step, const AuthoredArguments* arguments)
        {
            VerbInvocation invocation;
            invocation.Owner = Holder;
            invocation.Step = step;
            invocation.Tick = kStart;
            invocation.Arguments = arguments;
            return Operations.Light(invocation);
        }
    };
}

TEST(AttachedLight, StartsAtThePeakAndSettlesToTheSteadyBrightness)
{
    LightFixture fx;
    ASSERT_EQ(fx.State.Hold(fx.Entities, fx.Spec(), kStart, fx.Holder), AttachResult::Attached);
    EXPECT_NEAR(fx.At(0.0), 12.0f, 1e-3f);
    const float early = fx.At(0.05);
    EXPECT_LT(early, 12.0f);
    EXPECT_GT(early, 4.0f);
    EXPECT_NEAR(fx.At(2.0), 4.0f, 1e-3f);
}

TEST(AttachedLight, HasOnePulseSoItNeverRisesAgainWhileHeld)
{
    LightFixture fx;
    ASSERT_EQ(fx.State.Hold(fx.Entities, fx.Spec(0.03f), kStart, fx.Holder), AttachResult::Attached);
    float highest = 0.0f;
    bool falling = false;
    for (int frame = 0; frame < 240; ++frame)
    {
        const float now = fx.At(frame / 240.0);
        if (falling)
        {
            EXPECT_LE(now, highest + 1e-4f) << "frame " << frame;
        }
        falling = falling || now < highest;
        highest = falling ? now : std::max(highest, now);
    }
}

TEST(AttachedLight, ReducedFlashingNeverExceedsTheSteadyBrightnessAndRampsSlowly)
{
    LightFixture fx;
    fx.State.Settings.ReducedFlashing = true;
    ASSERT_EQ(fx.State.Hold(fx.Entities, fx.Spec(0.0f, 0.0f), kStart, fx.Holder), AttachResult::Attached);
    EXPECT_NEAR(fx.At(0.0), 0.0f, 1e-3f);
    for (double t = 0.0; t < 1.0; t += 1.0 / 120.0)
        EXPECT_LE(fx.At(t), 4.0f + 1e-3f);
    EXPECT_LT(fx.At(0.1), 4.0f) << "still rising a tenth of a second in";
    EXPECT_NEAR(fx.At(0.25), 4.0f, 1e-3f);
}

TEST(AttachedLight, FadesAfterReleaseAndThenEnds)
{
    LightFixture fx;
    ASSERT_EQ(fx.State.Hold(fx.Entities, fx.Spec(0.0f, 0.2f), kStart, fx.Holder), AttachResult::Attached);
    fx.State.Release(fx.Holder, kStart + 60, kTick);
    EXPECT_NEAR(fx.At(1.0), 4.0f, 0.1f);
    const float fading = fx.At(1.1);
    EXPECT_LT(fading, 4.0f);
    EXPECT_GT(fading, 0.0f);
    (void)fx.At(1.21);
    EXPECT_FALSE(fx.Worn().IsValid());
    EXPECT_EQ(fx.State.Count(), 0u);
}

TEST(AttachedLight, StandsStillWhileTheSimulationClockDoes)
{
    LightFixture fx;
    ASSERT_EQ(fx.State.Hold(fx.Entities, fx.Spec(), kStart, fx.Holder), AttachResult::Attached);
    const float first = fx.At(0.05);
    EXPECT_EQ(fx.At(0.05), first);
}

TEST(AttachedLight, EndsWithTheBodyThatWearsIt)
{
    LightFixture fx;
    ASSERT_EQ(fx.State.Hold(fx.Entities, fx.Spec(), kStart, fx.Holder), AttachResult::Attached);
    const EntityId worn = fx.Worn();
    fx.Entities.DestroyEntity(fx.Body);
    PlaceAttachments(fx.Entities, 0.0);
    EXPECT_FALSE(fx.Entities.IsAlive(worn));
    (void)fx.At(0.1);
    EXPECT_EQ(fx.State.Count(), 0u);
}

TEST(AttachedLight, EndsWithAHolderThatNeverLetGo)
{
    LightFixture fx;
    AttachedLightSpec spec = fx.Spec();
    spec.Attachment.Placement.Owner = { {}, fx.Holder };
    ASSERT_EQ(fx.State.Hold(fx.Entities, spec, kStart, fx.Holder), AttachResult::Attached);
    const EntityId worn = fx.Worn();
    fx.Entities.DestroyEntity(fx.Holder.Entity);
    PlaceAttachments(fx.Entities, 0.0);
    EXPECT_FALSE(fx.Entities.IsAlive(worn));
    (void)fx.At(0.0);
    EXPECT_EQ(fx.State.Count(), 0u);
}

TEST(AttachedLight, HoldingAgainChangesTheLightInPlace)
{
    LightFixture fx;
    ASSERT_EQ(fx.State.Hold(fx.Entities, fx.Spec(), kStart, fx.Holder), AttachResult::Attached);
    const EntityId worn = fx.Worn();
    AttachedLightSpec spec = fx.Spec();
    spec.Attachment.Light.Range = 9.0f;
    EXPECT_EQ(fx.State.Hold(fx.Entities, spec, kStart + 5, fx.Holder), AttachResult::Updated);
    EXPECT_EQ(fx.Worn(), worn);
    EXPECT_EQ(fx.Entities.TryGet<PointLightComponent>(worn)->Range, 9.0f);
    EXPECT_EQ(fx.State.Count(), 1u);
}

TEST(PresentationLightLease, AHolderHoldsALightUntilItLetsGo)
{
    LightVerbFixture fx;
    const AuthoredArguments arguments = fx.Arguments();
    ASSERT_EQ(fx.Step(LeaseStep::Acquire, &arguments), VerbAdmission::Accepted);
    const EntityId worn = fx.Worn();
    ASSERT_TRUE(worn.IsValid());
    const PointLightComponent& light = *fx.Entities.TryGet<PointLightComponent>(worn);
    EXPECT_FLOAT_EQ(light.Color.Z, 1.0f);
    EXPECT_FLOAT_EQ(light.Range, 6.0f);
    EXPECT_TRUE(light.PacksFirst);
    EXPECT_FALSE(light.CastShadows);

    ASSERT_EQ(fx.Step(LeaseStep::Release, nullptr), VerbAdmission::Accepted);
    (void)fx.At(0.1);
    EXPECT_TRUE(fx.Worn().IsValid()) << "it fades out rather than cuts";
    (void)fx.At(0.5);
    EXPECT_FALSE(fx.Worn().IsValid());
}

TEST(PresentationLightLease, RefusesABrightnessOrRangeItCouldNotDraw)
{
    LightVerbFixture fx;
    const AuthoredArguments arguments = fx.Arguments(0.0);
    EXPECT_EQ(fx.Step(LeaseStep::Acquire, &arguments), VerbAdmission::InvalidArguments);
    EXPECT_FALSE(fx.Worn().IsValid());
}
