#include <gtest/gtest.h>

#include <camera/CameraEffectTiming.h>
#include <ecs/World.h>
#include <presentation/PresentationContributions.h>
#include <render/post/PostEffectCatalog.h>
#include <render/post/PostEffectContributions.h>

#include <array>
#include <cstdint>

namespace
{
    constexpr std::array<std::uint32_t, 1> kShader{ 0x07230203u };

    PostEffectDesc Desc(std::string_view name, PostEffectDomain domain, std::int16_t order)
    {
        return { .Name = name, .FragmentSpirv = kShader, .Domain = domain, .Order = order };
    }

    PresentationOwner Writer(std::uint32_t writer, EntityId holder = {})
    {
        return { .Writer = PresentationWriterId{ writer }, .Holder = LeaseOwner{ .Entity = holder, .Key = 0 } };
    }

    PostEffectContribution Contribution(PresentationOwner owner, float weight, std::int16_t order = 0)
    {
        PostEffectContribution contribution;
        contribution.Owner = owner;
        contribution.Order = order;
        contribution.Weight = weight;
        return contribution;
    }

    struct Fixture
    {
        World Entities;
        PostEffectCatalog Catalog;
        EntityId Camera = Entities.CreateEntity();
        PostEffectId Fade = Catalog.Register(Desc("fade", PostEffectDomain::Display, 10));
        PostEffectId Warp = Catalog.Register(Desc("warp", PostEffectDomain::Scene, 10));
        PostEffectId Tear = Catalog.Register(Desc("tear", PostEffectDomain::Display, 1));

        [[nodiscard]] PostEffectFrame Frame(EntityId camera) const
        {
            PostEffectFrame frame;
            ExtractPostEffects(Entities, camera, Catalog, 1.5f, frame);
            return frame;
        }
        [[nodiscard]] PostEffectFrame Frame() const { return Frame(Camera); }
    };
}

TEST(PostEffectCatalog, RefusesADuplicateNameOrAnOccupiedOrderInADomain)
{
    PostEffectCatalog catalog;
    const PostEffectId first = catalog.Register(Desc("a", PostEffectDomain::Display, 1));
    ASSERT_TRUE(first.IsValid());
    EXPECT_FALSE(catalog.Register(Desc("a", PostEffectDomain::Scene, 2)).IsValid());
    EXPECT_FALSE(catalog.Register(Desc("b", PostEffectDomain::Display, 1)).IsValid());
    EXPECT_TRUE(catalog.Register(Desc("b", PostEffectDomain::Scene, 1)).IsValid()) << "orders are per domain";
    EXPECT_EQ(catalog.Find("a"), first);
    EXPECT_FALSE(catalog.Find("missing").IsValid());
}

TEST(PostEffectCatalog, RefusesAnEffectWithNoShader)
{
    PostEffectCatalog catalog;
    EXPECT_FALSE(catalog.Register(PostEffectDesc{ .Name = "empty", .FragmentSpirv = {} }).IsValid());
}

TEST(PostEffectCatalog, KeepsEachRefusalWithItsReason)
{
    PostEffectCatalog catalog;
    ASSERT_TRUE(catalog.Register(Desc("a", PostEffectDomain::Display, 1)).IsValid());
    EXPECT_TRUE(catalog.Refusals().empty());
    (void)catalog.Register(Desc("a", PostEffectDomain::Scene, 2));
    (void)catalog.Register(Desc("b", PostEffectDomain::Display, 1));
    ASSERT_EQ(catalog.Refusals().size(), 2u);
    EXPECT_EQ(catalog.Refusals()[0], "post effect 'a' refused: the name is taken");
    EXPECT_EQ(catalog.Refusals()[1], "post effect 'b' refused: 'a' already runs at that order in that domain");
}

TEST(PostEffects, TheHighestOrderWinsAnEffectAndTheTieBreakIsTheSameEitherWay)
{
    Fixture f;
    SetPostEffect(f.Entities, f.Fade, Contribution(Writer(1), 0.25f, 0));
    SetPostEffect(f.Entities, f.Fade, Contribution(Writer(2), 0.75f, 5));
    ASSERT_EQ(f.Frame().Entries.size(), 1u) << "one instance of a type per view";
    EXPECT_FLOAT_EQ(f.Frame().Entries[0].Weight, 0.75f);

    for (const bool lowFirst : { true, false })
    {
        Fixture tied;
        const auto low = Contribution(Writer(1), 0.25f);
        const auto high = Contribution(Writer(2), 0.75f);
        SetPostEffect(tied.Entities, tied.Fade, lowFirst ? low : high);
        SetPostEffect(tied.Entities, tied.Fade, lowFirst ? high : low);
        EXPECT_FLOAT_EQ(tied.Frame().Entries[0].Weight, 0.25f) << "the owner that precedes, whatever arrived first";
    }
}

TEST(PostEffects, AWriterReplacesItsOwnContributionAndClearsIt)
{
    Fixture f;
    SetPostEffect(f.Entities, f.Fade, Contribution(Writer(1), 0.25f));
    SetPostEffect(f.Entities, f.Fade, Contribution(Writer(1), 0.5f));
    ASSERT_EQ(f.Frame().Entries.size(), 1u);
    EXPECT_FLOAT_EQ(f.Frame().Entries[0].Weight, 0.5f);
    ClearPostEffect(f.Entities, f.Fade, Writer(1));
    EXPECT_TRUE(f.Frame().Entries.empty());
}

TEST(PostEffects, AnAudienceLimitsAnEffectToOneSubjectsView)
{
    Fixture f;
    const EntityId subjectA = f.Entities.CreateEntity();
    const EntityId subjectB = f.Entities.CreateEntity();
    auto forA = Contribution(Writer(1), 0.5f);
    forA.AudienceSubject = subjectA;
    SetPostEffect(f.Entities, f.Fade, forA);

    EXPECT_EQ(f.Frame(subjectA).Entries.size(), 1u) << "a camera that is the subject sees it";
    EXPECT_TRUE(f.Frame(subjectB).Entries.empty());

    SetPostEffect(f.Entities, f.Warp, Contribution(Writer(2), 0.5f));
    EXPECT_EQ(f.Frame(subjectB).Entries.size(), 1u) << "no audience means every view";
}

TEST(PostEffects, TheFrameIsInDomainThenRegisteredOrderAndOmitsWeightZero)
{
    Fixture f;
    SetPostEffect(f.Entities, f.Fade, Contribution(Writer(1), 0.5f));
    SetPostEffect(f.Entities, f.Tear, Contribution(Writer(1), 0.5f));
    SetPostEffect(f.Entities, f.Warp, Contribution(Writer(1), 0.5f));

    PostEffectFrame frame = f.Frame();
    ASSERT_EQ(frame.Entries.size(), 3u);
    EXPECT_EQ(frame.Entries[0].Effect, f.Warp);
    EXPECT_EQ(frame.Entries[1].Effect, f.Tear);
    EXPECT_EQ(frame.Entries[2].Effect, f.Fade);
    EXPECT_FLOAT_EQ(frame.WallSeconds, 1.5f);

    SetPostEffect(f.Entities, f.Tear, Contribution(Writer(1), 0.0f));
    frame = f.Frame();
    ASSERT_EQ(frame.Entries.size(), 2u);
    EXPECT_EQ(frame.Entries[1].Effect, f.Fade);
}

TEST(PostEffects, AHeldContributionEndsWithItsHolder)
{
    Fixture f;
    const EntityId holder = f.Entities.CreateEntity();
    SetPostEffect(f.Entities, f.Fade, Contribution(Writer(1, holder), 0.5f));
    AdvancePostEffectLifetimes(f.Entities, 0.016f);
    EXPECT_EQ(f.Frame().Entries.size(), 1u);

    f.Entities.DestroyEntity(holder);
    AdvancePostEffectLifetimes(f.Entities, 0.016f);
    EXPECT_TRUE(f.Frame().Entries.empty());
}

TEST(PostEffects, ASessionContributionOutlivesItsEntityAndEndsWithItsWriter)
{
    Fixture f;
    const EntityId holder = f.Entities.CreateEntity();
    auto session = Contribution(Writer(1, holder), 0.5f);
    session.Lifetime = PostEffectLifetime::Session;
    SetPostEffect(f.Entities, f.Fade, session);
    SetPostEffect(f.Entities, f.Warp, Contribution(Writer(2), 0.5f));

    f.Entities.DestroyEntity(holder);
    AdvancePostEffectLifetimes(f.Entities, 100.0f);
    EXPECT_EQ(f.Frame().Entries.size(), 2u) << "a session has no cap unless it names one";

    ClearPostEffectsOf(f.Entities, PresentationWriterId{ 1 });
    ASSERT_EQ(f.Frame().Entries.size(), 1u);
    EXPECT_EQ(f.Frame().Entries[0].Effect, f.Warp) << "only that writer's";
}

TEST(PostEffects, AWallClockCapEndsASessionWhileNothingElseAdvances)
{
    Fixture f;
    auto capped = Contribution(Writer(1), 0.5f);
    capped.Lifetime = PostEffectLifetime::Session;
    capped.RemainingWallSeconds = 1.0f;
    SetPostEffect(f.Entities, f.Fade, capped);

    AdvancePostEffectLifetimes(f.Entities, 0.5f);
    EXPECT_EQ(f.Frame().Entries.size(), 1u);
    AdvancePostEffectLifetimes(f.Entities, 0.75f);
    EXPECT_TRUE(f.Frame().Entries.empty());
}

TEST(PostEffects, AWorldWithNoContributionsExtractsNothing)
{
    Fixture f;
    EXPECT_TRUE(f.Frame().Entries.empty());
    AdvancePostEffectLifetimes(f.Entities, 1.0f);
    ClearPostEffectsOf(f.Entities, PresentationWriterId{ 1 });
}
