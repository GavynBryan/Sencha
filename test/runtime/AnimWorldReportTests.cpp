// What a World's animated entities carry, per rig, and the requests they were
// given that nothing played.

#include "AnimRigFixture.h"

#include <anim/AnimDecisionLog.h>
#include <anim/AnimWorldReport.h>
#include <core/console/ConsoleService.h>

#include <format>

namespace
{
    struct ReportFixture
    {
        AnimRigFixture Fx;
        DataAssetHandle Rig;

        ReportFixture()
        {
            AnimCharacterRig::RegisterTags(Fx);
            Rig = AnimCharacterRig::Load(Fx);
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
    ReportFixture fixture;
    const EntityId prop = fixture.Prop();
    const EntityId simple = fixture.Prop();
    fixture.Fx.Entities.AddComponent(simple, AnimFacts{});
    const EntityId character = fixture.Prop();
    fixture.Fx.Entities.AddComponent(character, AnimFactsLarge{});
    fixture.Fx.Entities.AddComponent(character, AnimDecisionLog{});

    const World& world = fixture.Fx.Entities;
    const std::uint32_t propBytes = AnimEntityBytes(world, prop);
    EXPECT_EQ(propBytes, sizeof(AnimRig) + sizeof(AnimRequestSet) + sizeof(AnimContentState) + sizeof(AnimFlowState));
    const std::uint32_t selection = sizeof(AnimFactHistory) + sizeof(AnimSelectorState);
    EXPECT_EQ(AnimEntityBytes(world, simple), propBytes + sizeof(AnimFacts) + selection);
    EXPECT_EQ(AnimEntityBytes(world, character),
              propBytes + sizeof(AnimFactsLarge) + selection + sizeof(AnimDecisionLog));
    EXPECT_EQ(AnimEntityBytes(world, fixture.Fx.Entities.CreateEntity()), 0u);

    RecordProperty("prop_bytes", static_cast<int>(propBytes));
    RecordProperty("simple_bytes", static_cast<int>(AnimEntityBytes(world, simple)));
    RecordProperty("character_bytes", static_cast<int>(AnimEntityBytes(world, character)));
}

TEST(AnimWorldReport, EntitiesAreReportedByTheirRig)
{
    ReportFixture fixture;
    const EntityId prop = fixture.Prop();
    const EntityId simple = fixture.Prop();
    fixture.Fx.Entities.AddComponent(simple, AnimFacts{});

    const std::vector<AnimRigWorldReport> reports = ReportAnimWorld(fixture.Fx.Entities);
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].RigPath, "asset://anim/character.rig.sdata");
    EXPECT_EQ(reports[0].Entities, 2u);
    EXPECT_EQ(reports[0].MinEntityBytes, AnimEntityBytes(fixture.Fx.Entities, prop));
    EXPECT_EQ(reports[0].MaxEntityBytes, AnimEntityBytes(fixture.Fx.Entities, simple));
}

// A reload the selector plays is not counted; one issued while death holds
// every layer ends without playing, and is.
TEST(AnimWorldReport, ARequestThatEndsUnplayedIsCounted)
{
    ReportFixture fixture;
    const EntityId animated = fixture.Fx.Character(fixture.Rig);
    fixture.Fx.Tick();

    ASSERT_EQ(fixture.IssueFor(animated, 3).Status, AnimRequestStatus::Accepted);
    fixture.Fx.Tick();
    EXPECT_EQ(fixture.Fx.BehaviorName(animated), "Anim.Action.Reload");
    fixture.Fx.Tick(90);
    EXPECT_EQ(fixture.Fx.BehaviorName(animated), "Anim.Locomotion.Idle") << "the reload has played through";
    EXPECT_EQ(fixture.Unplayed(animated), 0u);

    fixture.Fx.Motion(animated).Dead = true;
    fixture.Fx.Tick();
    ASSERT_EQ(fixture.IssueFor(animated, 3).Status, AnimRequestStatus::Accepted);
    fixture.Fx.Tick(2);
    EXPECT_EQ(fixture.Unplayed(animated), 0u) << "still live: it could yet play";
    fixture.Fx.Tick(8);
    EXPECT_EQ(fixture.Fx.BehaviorName(animated), "Anim.Death");
    EXPECT_EQ(fixture.Unplayed(animated), 1u);

    const std::vector<AnimRigWorldReport> reports = ReportAnimWorld(fixture.Fx.Entities);
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].UnplayedRequests, 1u);
}

TEST(AnimWorldReport, TheRiskCommandReportsEntitiesAndUnplayedRequests)
{
    ReportFixture fixture;
    ConsoleService console;
    RegisterAnimationConsole(console.Registry(), fixture.Fx.Entities);
    const EntityId prop = fixture.Prop();
    fixture.Fx.Tick();

    const ConsoleResult report = console.ExecuteLine("anim.risk");
    ASSERT_TRUE(report.Succeeded());
    std::string text;
    for (const ConsoleOutputEntry& entry : report.Output)
        text += entry.Text + "\n";
    EXPECT_NE(text.find(std::format("  1 entities at {} bytes each; 0 requests went unplayed",
                                    AnimEntityBytes(fixture.Fx.Entities, prop))),
              std::string::npos)
        << text;
}
