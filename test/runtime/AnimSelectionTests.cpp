// Selection is a pure function of facts, requests, the previous tick's feedback
// and selector state, driven here through the real gather, select and resolve
// systems and read back as winners, verdicts and decision records.

#include "AnimRigFixture.h"

#include <cmath>

namespace
{
    struct CharacterRigFixture : AnimRigFixture
    {
        using AnimRigFixture::Clip;

        DataAssetHandle Rig;
        EntityId Entity;

        CharacterRigFixture()
        {
            AnimCharacterRig::RegisterTags(*this);
            Rig = AnimCharacterRig::Load(*this);
            const AnimBoundRig& bound = Bound(Rig);
            EXPECT_TRUE(bound.Valid) << Describe(bound);
            Entity = Character(Rig);
        }

        std::string Behavior() { return BehaviorName(Entity); }
        std::string Clip() { return ClipName(Entity, Bound(Rig)); }

        int RuleIndex(std::string_view label)
        {
            const AnimBoundSelector& selector = Bound(Rig).Selectors[0];
            for (std::size_t i = 0; i < selector.Rules.size(); ++i)
                if (selector.Rules[i].Label == label)
                    return static_cast<int>(i);
            return -1;
        }

        // Verdicts for the tick about to run, from a copy of the state.
        std::vector<AnimRuleVerdict> Explain()
        {
            const AnimBoundRig& rig = Bound(Rig);
            AnimSelectorState copy = *Entities.TryGet<AnimSelectorState>(Entity);
            Gather.Gather(Entities, Now, kTick);
            const AnimFacts& facts = *Entities.TryGet<AnimFacts>(Entity);
            std::vector<std::vector<AnimRuleVerdict>> verdicts;
            SelectAnimEntity(Entities, Entity, rig,
                             std::span<const std::uint32_t>(facts.Values, rig.Slots.size()), copy, Now, kTick,
                             nullptr, &verdicts);
            return verdicts[0];
        }
    };
}

TEST(AnimSelection, TheSelectorFlattensInPriorityOrder)
{
    CharacterRigFixture fx;
    const AnimBoundSelector& selector = fx.Bound(fx.Rig).Selectors[0];
    std::vector<std::string> order;
    for (const AnimBoundRule& rule : selector.Rules)
        order.push_back(rule.Label);
    EXPECT_EQ(order, (std::vector<std::string>{ "death", "reload", "land", "sprint", "walk", "idle" }));
    EXPECT_EQ(fx.Bound(fx.Rig).Layers[0].Selector, 0);
}

TEST(AnimSelection, SpeedChangesTheWinnerAndTheResolvedClip)
{
    CharacterRigFixture fx;
    fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Locomotion.Idle");
    EXPECT_EQ(fx.Clip(), "asset://anim/idle.sanim");

    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Locomotion.Walk");
    EXPECT_EQ(fx.Clip(), "asset://anim/walk.sanim");
    const AnimDecisionRecord* changed = fx.LastRecord(fx.Entity, AnimDecisionCause::WinnerChanged);
    ASSERT_NE(changed, nullptr);
    EXPECT_EQ(changed->Reason, AnimChangeReason::FactsChanged);
    EXPECT_EQ(changed->Rule, fx.RuleIndex("walk"));
    EXPECT_EQ(changed->PreviousRule, fx.RuleIndex("idle"));

    // Enter at 2.2, stay above 1.8: the boundary cannot flicker.
    fx.Motion(fx.Entity).Speed = 2.0f;
    fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Locomotion.Walk");
    fx.Motion(fx.Entity).Speed = 2.5f;
    fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Locomotion.Sprint");
    EXPECT_EQ(fx.Clip(), "asset://anim/sprint.sanim");
    fx.Motion(fx.Entity).Speed = 2.0f;
    fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Locomotion.Sprint");
    fx.Motion(fx.Entity).Speed = 1.5f;
    fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Locomotion.Walk");
}

TEST(AnimSelection, WhyARuleLostIsExplained)
{
    CharacterRigFixture fx;
    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Tick();

    const std::vector<AnimRuleVerdict> verdicts = fx.Explain();
    const AnimRuleVerdict& sprint = verdicts[static_cast<std::size_t>(fx.RuleIndex("sprint"))];
    EXPECT_EQ(sprint.Kind, AnimRuleVerdictKind::Failed);
    EXPECT_EQ(sprint.Evaluation.FailedRow, 0);
    EXPECT_FLOAT_EQ(static_cast<float>(sprint.Evaluation.Observed), 1.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(sprint.Evaluation.Expected), 2.2f);

    const AnimRuleVerdict& walk = verdicts[static_cast<std::size_t>(fx.RuleIndex("walk"))];
    EXPECT_EQ(walk.Kind, AnimRuleVerdictKind::Winner);
    EXPECT_TRUE(walk.Stayed);
    EXPECT_EQ(verdicts[static_cast<std::size_t>(fx.RuleIndex("idle"))].Kind, AnimRuleVerdictKind::NotEvaluated);
    EXPECT_EQ(verdicts[static_cast<std::size_t>(fx.RuleIndex("death"))].Kind, AnimRuleVerdictKind::Failed);

    // Explaining never moves the real state.
    EXPECT_EQ(fx.Selection(fx.Entity).Winner, fx.RuleIndex("walk"));
}

TEST(AnimSelection, ALatchHoldsAOneShotUntilItsContentCompletes)
{
    CharacterRigFixture fx;
    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Motion(fx.Entity).Grounded = false;
    fx.Tick(5);
    fx.Motion(fx.Entity).Grounded = true;
    fx.Tick();
    const AnimTick landed = fx.Last();
    EXPECT_EQ(fx.Behavior(), "Anim.Action.Land");
    EXPECT_EQ(fx.Selection(fx.Entity).Latch, AnimLatchState::Held);
    ASSERT_NE(fx.LastRecord(fx.Entity, AnimDecisionCause::LatchArmed), nullptr);

    // The edge that selected it lasts 100 ms; the clip lasts 250. The latch,
    // not the edge, keeps it, and walking is not allowed to interrupt.
    fx.Tick(8);
    EXPECT_EQ(fx.Behavior(), "Anim.Action.Land");
    const std::vector<AnimRuleVerdict> verdicts = fx.Explain();
    EXPECT_EQ(verdicts[static_cast<std::size_t>(fx.RuleIndex("land"))].Kind, AnimRuleVerdictKind::Winner);
    EXPECT_EQ(verdicts[static_cast<std::size_t>(fx.RuleIndex("walk"))].Kind, AnimRuleVerdictKind::NotEvaluated);

    // Content completes 250 ms (15 ticks) after it started; selection reads
    // that one tick later and releases.
    while (fx.Behavior() == "Anim.Action.Land" && fx.Now < landed + 30)
        fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Locomotion.Walk");
    EXPECT_EQ(fx.Last(), landed + 16);
    const AnimDecisionRecord* released = fx.LastRecord(fx.Entity, AnimDecisionCause::WinnerChanged);
    ASSERT_NE(released, nullptr);
    EXPECT_EQ(released->Reason, AnimChangeReason::LatchComplete);
    EXPECT_NE(fx.LastRecord(fx.Entity, AnimDecisionCause::LatchReleased), nullptr);
}

TEST(AnimSelection, OnlyWhatTheLatchAllowsInterruptsIt)
{
    CharacterRigFixture fx;
    fx.Motion(fx.Entity).Grounded = false;
    fx.Tick(5);
    fx.Motion(fx.Entity).Grounded = true;
    fx.Tick();
    ASSERT_EQ(fx.Behavior(), "Anim.Action.Land");

    // A reload is priority 50: it passes but may not interrupt a land that
    // only yields to 100 and above.
    ASSERT_TRUE(fx.Issue(fx.Entity, "anim.intent.reload").Accepted());
    const std::vector<AnimRuleVerdict> verdicts = fx.Explain();
    EXPECT_EQ(verdicts[static_cast<std::size_t>(fx.RuleIndex("reload"))].Kind, AnimRuleVerdictKind::BlockedByLatch);
    fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Action.Land");

    // Death is 100.
    fx.Motion(fx.Entity).Dead = true;
    fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Death");
    const AnimDecisionRecord* changed = fx.LastRecord(fx.Entity, AnimDecisionCause::WinnerChanged);
    ASSERT_NE(changed, nullptr);
    EXPECT_EQ(changed->Reason, AnimChangeReason::LatchInterrupted);
    EXPECT_NE(fx.LastRecord(fx.Entity, AnimDecisionCause::LatchInterrupted), nullptr);
}

TEST(AnimSelection, ARequestLatchFinishesItsContentAfterTheRequestEnds)
{
    CharacterRigFixture fx;
    fx.Tick();
    const AnimRequestResult reload = fx.Issue(fx.Entity, "anim.intent.reload");
    ASSERT_TRUE(reload.Accepted());
    fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Action.Reload");
    EXPECT_EQ(fx.Selection(fx.Entity).LatchRequest, reload.Id);
    const AnimDecisionRecord* changed = fx.LastRecord(fx.Entity, AnimDecisionCause::WinnerChanged);
    ASSERT_NE(changed, nullptr);
    EXPECT_EQ(changed->Reason, AnimChangeReason::RequestsChanged);

    fx.Tick(10);
    ASSERT_TRUE(CancelAnimRequest(fx.Entities, fx.Entity, reload.Id, AnimCancelReason::Released, fx.Now));
    fx.Tick();
    // Finish: the request is gone but the one-shot plays out.
    EXPECT_EQ(fx.Behavior(), "Anim.Action.Reload");
    EXPECT_EQ(fx.Selection(fx.Entity).Latch, AnimLatchState::Finishing);

    fx.Tick(60);
    EXPECT_EQ(fx.Behavior(), "Anim.Locomotion.Idle");
    EXPECT_EQ(fx.Selection(fx.Entity).Latch, AnimLatchState::None);
}

TEST(AnimSelection, AnIdleEntityIsNotReevaluated)
{
    CharacterRigFixture fx;
    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Tick(3);
    EXPECT_EQ(fx.Select.Evaluated(), 0u);
    EXPECT_EQ(fx.Select.Skipped(), 1u);

    fx.Motion(fx.Entity).Speed = 3.0f;
    fx.Tick();
    EXPECT_EQ(fx.Select.Evaluated(), 1u);
    EXPECT_EQ(fx.Behavior(), "Anim.Locomotion.Sprint");
}

// A small selector exercising holds and cooldowns, where the interesting
// changes happen with no input changing at all.
namespace
{
    struct Timers : AnimRigFixture
    {
        DataAssetHandle Rig;
        EntityId Entity;

        Timers()
            : AnimRigFixture({ "Anim.Burst", "Anim.Rest" })
        {
            Clip("asset://anim/burst.sanim", 0.5f);
            Clip("asset://anim/rest.sanim", 1.0f);
            (void)Load("asset://anim/t.facts.sdata", kAnimFactSchemaType, R"({
                "slots": [ { "name": "Crouched", "kind": "bool" } ] })");
            (void)Load("asset://anim/t.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
                { "tag": "Anim.Burst", "kind": "cyclic" }, { "tag": "Anim.Rest", "kind": "cyclic" } ] })");
            (void)Load("asset://anim/t.selector.sdata", kAnimSelectorType, R"({ "rules": [
                { "name": "burst", "priority": 10, "enter": [ { "fact": "Crouched" } ], "behavior": "Anim.Burst",
                  "hold_min_ms": 100, "cooldown_ms": 200 },
                { "name": "rest", "priority": 0, "enter": [], "behavior": "Anim.Rest" } ] })");
            (void)Load("asset://anim/t.slots.sdata", kAnimSlotMapType, R"({ "rows": [
                { "behavior": "Anim.Burst", "clip": "asset://anim/burst.sanim" },
                { "behavior": "Anim.Rest", "clip": "asset://anim/rest.sanim" } ] })");
            Rig = Load("asset://anim/t.rig.sdata", kAnimRigType, R"({
                "facts": "asset://anim/t.facts.sdata", "behaviors": [ "asset://anim/t.behaviors.sdata" ],
                "slot_maps": [ "asset://anim/t.slots.sdata" ],
                "layers": [ { "name": "anim.layer.base", "selector": "asset://anim/t.selector.sdata" } ] })");
            EXPECT_TRUE(Bound(Rig).Valid) << Describe(Bound(Rig));
            Entity = Character(Rig);
        }
        std::string Behavior() { return BehaviorName(Entity); }
    };
}

TEST(AnimSelection, AHoldKeepsAWinnerAndATimerWakesItWithoutNewInput)
{
    Timers fx;
    fx.Motion(fx.Entity).Crouched = true;
    fx.Tick();
    ASSERT_EQ(fx.Behavior(), "Anim.Burst");
    const AnimTick won = fx.Last();

    // The stay fails at once; the 100 ms hold (6 ticks) keeps rest out.
    fx.Motion(fx.Entity).Crouched = false;
    fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Burst");
    std::size_t skipped = 0;
    while (fx.Behavior() == "Anim.Burst" && fx.Now < won + 20)
    {
        fx.Tick();
        skipped += fx.Select.Skipped();
    }
    // Nothing changed after the tick crouch dropped, so every tick in between
    // was skipped, and the hold's expiry still woke the entity on time.
    EXPECT_EQ(fx.Behavior(), "Anim.Rest");
    EXPECT_EQ(fx.Last(), won + 6);
    EXPECT_EQ(skipped, 4u);
    const AnimDecisionRecord* changed = fx.LastRecord(fx.Entity, AnimDecisionCause::WinnerChanged);
    ASSERT_NE(changed, nullptr);
    EXPECT_EQ(changed->Reason, AnimChangeReason::HoldExpired);

    // Losing started a 200 ms cooldown: crouching again does not win.
    fx.Motion(fx.Entity).Crouched = true;
    fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Rest");
    std::vector<std::vector<AnimRuleVerdict>> verdicts;
    AnimSelectorState copy = *fx.Entities.TryGet<AnimSelectorState>(fx.Entity);
    const AnimFacts& facts = *fx.Entities.TryGet<AnimFacts>(fx.Entity);
    SelectAnimEntity(fx.Entities, fx.Entity, fx.Bound(fx.Rig), std::span<const std::uint32_t>(facts.Values, 1),
                     copy, fx.Now, AnimRigFixture::kTick, nullptr, &verdicts);
    EXPECT_EQ(verdicts[0][0].Kind, AnimRuleVerdictKind::Cooldown);

    // And wins when it ends, woken by the cooldown timer.
    const AnimTick lost = won + 6;
    while (fx.Behavior() == "Anim.Rest" && fx.Now < lost + 30)
        fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Burst");
    EXPECT_EQ(fx.Last(), lost + 12);
}

TEST(AnimSelection, ARebindKeepsTheWinnerByItsKeyAndResetsItWhenItIsGone)
{
    Timers fx;
    fx.Tick();
    ASSERT_EQ(fx.Behavior(), "Anim.Rest");

    // Reordered: the same rules at different indices.
    fx.Reload("asset://anim/t.selector.sdata", kAnimSelectorType, R"({ "rules": [
        { "name": "rest", "priority": 0, "enter": [], "behavior": "Anim.Rest" },
        { "name": "burst", "priority": 10, "enter": [ { "fact": "Crouched" } ], "behavior": "Anim.Burst" } ] })");
    fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Rest");
    EXPECT_EQ(fx.LastRecord(fx.Entity, AnimDecisionCause::IndexReset), nullptr);

    // Removed: the winner cannot be kept and the reset is recorded.
    fx.Reload("asset://anim/t.selector.sdata", kAnimSelectorType, R"({ "rules": [
        { "name": "burst", "priority": 10, "enter": [], "behavior": "Anim.Burst" } ] })");
    fx.Tick();
    EXPECT_EQ(fx.Behavior(), "Anim.Burst");
    const AnimDecisionRecord* reset = fx.LastRecord(fx.Entity, AnimDecisionCause::IndexReset);
    ASSERT_NE(reset, nullptr);
    EXPECT_EQ(reset->Reason, AnimChangeReason::Rebound);
}
