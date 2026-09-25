// What a World's animated entities carry, per rig, and the requests they were
// given that nothing played.

#include "AnimRigFixture.h"

#include <anim/AnimDecisionLog.h>
#include <anim/AnimWorldReport.h>
#include <core/console/ConsoleService.h>

#include <format>

namespace
{
    struct ReportedHero
    {
        AnimRigFixture Fx;
        DataAssetHandle Rig;

        ReportedHero()
        {
            AnimHero::RegisterTags(Fx);
            Rig = AnimHero::Load(Fx);
        }

        EntityId Prop()
        {
            const EntityId entity = Fx.Entities.CreateEntity();
            Fx.Entities.AddComponent(entity, AnimRig{ Rig });
            return entity;
        }

        AnimRequestResult IssueFor(EntityId animated, AnimTick ticks)
        {
            AnimRequestDesc desc;
            desc.Source = animated;
            desc.Intent = Fx.Tag("anim.intent.reload");
            desc.Lifetime = AnimRequestLifetime::Fixed;
            desc.FixedTicks = ticks;
            return IssueAnimRequest(Fx.Entities, animated, desc, Fx.Now);
        }

        std::uint32_t Unplayed(EntityId entity)
        {
            return Fx.Entities.TryGet<AnimContentState>(entity)->UnplayedRequests;
        }
    };
}

// The tiers are one mechanism with more components, so each one's footprint is
// the one below it plus what it adds -- and a rig alone brings its derived state.
TEST(AnimWorldReport, FootprintsAreTheComponentsEachTierCarries)
{
    ReportedHero hero;
    const EntityId prop = hero.Prop();
    const EntityId simple = hero.Prop();
    hero.Fx.Entities.AddComponent(simple, AnimFacts{});
    const EntityId character = hero.Prop();
    hero.Fx.Entities.AddComponent(character, AnimFactsLarge{});
    hero.Fx.Entities.AddComponent(character, AnimDecisionLog{});

    const World& world = hero.Fx.Entities;
    const std::uint32_t propBytes = AnimEntityBytes(world, prop);
    EXPECT_EQ(propBytes, sizeof(AnimRig) + sizeof(AnimRequestSet) + sizeof(AnimContentState) + sizeof(AnimFlowState));
    const std::uint32_t selection = sizeof(AnimFactHistory) + sizeof(AnimSelectorState);
    EXPECT_EQ(AnimEntityBytes(world, simple), propBytes + sizeof(AnimFacts) + selection);
    EXPECT_EQ(AnimEntityBytes(world, character),
              propBytes + sizeof(AnimFactsLarge) + selection + sizeof(AnimDecisionLog));
    EXPECT_EQ(AnimEntityBytes(world, hero.Fx.Entities.CreateEntity()), 0u);

    RecordProperty("prop_bytes", static_cast<int>(propBytes));
    RecordProperty("simple_bytes", static_cast<int>(AnimEntityBytes(world, simple)));
    RecordProperty("character_bytes", static_cast<int>(AnimEntityBytes(world, character)));
}

TEST(AnimWorldReport, EntitiesAreReportedByTheirRig)
{
    ReportedHero hero;
    const EntityId prop = hero.Prop();
    const EntityId simple = hero.Prop();
    hero.Fx.Entities.AddComponent(simple, AnimFacts{});

    const std::vector<AnimRigWorldReport> reports = ReportAnimWorld(hero.Fx.Entities);
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].RigPath, "asset://anim/hero.rig.sdata");
    EXPECT_EQ(reports[0].Entities, 2u);
    EXPECT_EQ(reports[0].MinEntityBytes, AnimEntityBytes(hero.Fx.Entities, prop));
    EXPECT_EQ(reports[0].MaxEntityBytes, AnimEntityBytes(hero.Fx.Entities, simple));
}

// A reload the selector plays is not counted; one issued while death holds
// every layer ends without playing, and is.
TEST(AnimWorldReport, ARequestThatEndsUnplayedIsCounted)
{
    ReportedHero hero;
    const EntityId animated = hero.Fx.Character(hero.Rig);
    hero.Fx.Tick();

    ASSERT_EQ(hero.IssueFor(animated, 3).Status, AnimRequestStatus::Accepted);
    hero.Fx.Tick();
    EXPECT_EQ(hero.Fx.BehaviorName(animated), "Anim.Action.Reload");
    hero.Fx.Tick(90);
    EXPECT_EQ(hero.Fx.BehaviorName(animated), "Anim.Locomotion.Idle") << "the reload has played through";
    EXPECT_EQ(hero.Unplayed(animated), 0u);

    hero.Fx.Motion(animated).Dead = true;
    hero.Fx.Tick();
    ASSERT_EQ(hero.IssueFor(animated, 3).Status, AnimRequestStatus::Accepted);
    hero.Fx.Tick(2);
    EXPECT_EQ(hero.Unplayed(animated), 0u) << "still live: it could yet play";
    hero.Fx.Tick(8);
    EXPECT_EQ(hero.Fx.BehaviorName(animated), "Anim.Death");
    EXPECT_EQ(hero.Unplayed(animated), 1u);

    const std::vector<AnimRigWorldReport> reports = ReportAnimWorld(hero.Fx.Entities);
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].UnplayedRequests, 1u);
}

TEST(AnimWorldReport, TheRiskCommandReportsEntitiesAndUnplayedRequests)
{
    ReportedHero hero;
    ConsoleService console;
    RegisterAnimationConsole(console.Registry(), hero.Fx.Entities);
    const EntityId prop = hero.Prop();
    hero.Fx.Tick();

    const ConsoleResult report = console.ExecuteLine("anim.risk");
    ASSERT_TRUE(report.Succeeded());
    std::string text;
    for (const ConsoleOutputEntry& entry : report.Output)
        text += entry.Text + "\n";
    EXPECT_NE(text.find(std::format("  1 entities at {} bytes each; 0 requests went unplayed",
                                    AnimEntityBytes(hero.Fx.Entities, prop))),
              std::string::npos)
        << text;
}
