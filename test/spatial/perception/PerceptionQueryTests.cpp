// The authored perception queries answer from the frame, and answer
// Unavailable rather than false whenever the scan could not prove absence.

#include "PerceptionHarness.h"

#include <authored/AuthoredQueryDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <perception/PerceptionVocabulary.h>

#include <optional>
#include <vector>

namespace
{
    struct QueryHarness : PerceptionHarness
    {
        std::optional<AuthoredQueryDispatcher> Dispatcher;
        std::vector<AuthoredQueryBindingToken> Bindings;
        GameplayTagId Hostile;

        QueryHarness()
        {
            InstallAuthoredVocabulary(Entities);
            EXPECT_TRUE(InstallPerceptionVocabulary(Entities));
            Dispatcher.emplace(*FindAuthoredQueryRegistry(Entities));
            Bindings = BindPerceptionQueries(*Dispatcher, Entities);
            GameplayTagRegistry& tags = Entities.GetResource<GameplayTagRegistry>();
            Hostile = *tags.RegisterTag("faction.hostile");
            (void)tags.RegisterTag("faction.hostile.drone");
        }

        void Tag(EntityId entity, std::string_view name)
        {
            GameplayTagContainer container;
            (void)container.Grant(Entities.GetResource<GameplayTagRegistry>().FindTag(name));
            Entities.AddComponent<GameplayTagContainer>(entity, container);
        }

        AuthoredQueryStatus Ask(std::string_view query, std::vector<AuthoredValue> arguments, AuthoredValue& out)
        {
            return Dispatcher->Evaluate(Dispatcher->Registry().Resolve(query), arguments, out);
        }
    };
}

TEST(PerceptionQueries, SeesAnswersTrueFalseAndUnavailable)
{
    QueryHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero());
    const EntityId target = harness.Target(Vec3d(0, 0, -5));
    const EntityId hidden = harness.Target(Vec3d(0, 0, 5));
    AuthoredValue answer;

    // Before any scan the observer has no coverage: no answer, not "no".
    EXPECT_EQ(harness.Ask(kSeesQuery, { AuthoredValue::Entity(eye), AuthoredValue::Entity(target) }, answer),
              AuthoredQueryStatus::Unavailable);

    harness.Step();
    bool seen = false;
    ASSERT_EQ(harness.Ask(kSeesQuery, { AuthoredValue::Entity(eye), AuthoredValue::Entity(target) }, answer),
              AuthoredQueryStatus::Value);
    ASSERT_TRUE(answer.TryGetBool(seen));
    EXPECT_TRUE(seen);
    ASSERT_EQ(harness.Ask(kSeesQuery, { AuthoredValue::Entity(eye), AuthoredValue::Entity(hidden) }, answer),
              AuthoredQueryStatus::Value);
    ASSERT_TRUE(answer.TryGetBool(seen));
    EXPECT_FALSE(seen);
}

TEST(PerceptionQueries, APartialScanNeverAnswersFalse)
{
    QueryHarness harness;
    harness.SetCVar("perception.sight.rays_per_observer", CVarValue{ std::int64_t{ 1 } });
    const EntityId eye = harness.Observer(Vec3d::Zero());
    (void)harness.Target(Vec3d(0, 0, -5));
    const EntityId untested = harness.Target(Vec3d(0, 0, -8));
    harness.Step();

    AuthoredValue answer;
    EXPECT_EQ(harness.Ask(kSeesQuery, { AuthoredValue::Entity(eye), AuthoredValue::Entity(untested) }, answer),
              AuthoredQueryStatus::Unavailable);
}

TEST(PerceptionQueries, TaggedQueriesMatchBeneathTheTagAndPickTheNearest)
{
    QueryHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero());
    const EntityId far = harness.Target(Vec3d(0, 0, -9));
    const EntityId near = harness.Target(Vec3d(0, 0, -4));
    const EntityId friendly = harness.Target(Vec3d(0, 0, -2));
    harness.Tag(far, "faction.hostile");
    harness.Tag(near, "faction.hostile.drone");
    harness.Step();

    AuthoredValue answer;
    bool seen = false;
    ASSERT_EQ(harness.Ask(kSeesTaggedQuery, { AuthoredValue::Entity(eye), AuthoredValue::Tag(harness.Hostile) }, answer),
              AuthoredQueryStatus::Value);
    ASSERT_TRUE(answer.TryGetBool(seen));
    EXPECT_TRUE(seen);

    EntityId nearest;
    ASSERT_EQ(harness.Ask(kNearestSeenTaggedQuery,
                          { AuthoredValue::Entity(eye), AuthoredValue::Tag(harness.Hostile) }, answer),
              AuthoredQueryStatus::Value);
    ASSERT_TRUE(answer.TryGetEntity(nearest));
    EXPECT_EQ(nearest, near);
    (void)friendly;
}

TEST(PerceptionQueries, NearestAnswersUnavailableWhenNothingMatchingWasSeen)
{
    QueryHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero());
    (void)harness.Target(Vec3d(0, 0, -4));
    harness.Step();

    AuthoredValue answer;
    EXPECT_EQ(harness.Ask(kNearestSeenTaggedQuery,
                          { AuthoredValue::Entity(eye), AuthoredValue::Tag(harness.Hostile) }, answer),
              AuthoredQueryStatus::Unavailable);
    bool seen = true;
    ASSERT_EQ(harness.Ask(kSeesTaggedQuery, { AuthoredValue::Entity(eye), AuthoredValue::Tag(harness.Hostile) }, answer),
              AuthoredQueryStatus::Value);
    ASSERT_TRUE(answer.TryGetBool(seen));
    EXPECT_FALSE(seen);
}
