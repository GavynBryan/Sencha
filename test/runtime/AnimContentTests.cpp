// Content resolution turns a behavior into content every tick and applies the
// result by kind: cyclic content switches rows with normalized time carried
// across, one-shot content is pinned at entry. A request-keyed layer needs no
// facts and no selector at all, which is the whole Prop tier.

#include "AnimRigFixture.h"

namespace
{
    struct Hero : AnimRigFixture
    {
        using AnimRigFixture::Clip;

        DataAssetHandle Rig;
        EntityId Entity;

        Hero()
        {
            AnimHero::RegisterTags(*this);
            Rig = AnimHero::Load(*this);
            EXPECT_TRUE(Bound(Rig).Valid) << Describe(Bound(Rig));
            Entity = Character(Rig);
        }
        std::string Clip() { return ClipName(Entity, Bound(Rig)); }
    };
}

TEST(AnimContent, ARowChangeUnderACyclicBehaviorCarriesNormalizedTime)
{
    Hero fx;
    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Tick(31);
    ASSERT_EQ(fx.Clip(), "asset://anim/walk.sanim");
    // Walk won on tick 0; thirty ticks in, the 1 s clip is at 0.5.
    EXPECT_NEAR(fx.Playing(fx.Entity).TimeSeconds, 0.5f, 1e-4f);

    fx.Motion(fx.Entity).Crouched = true;
    fx.Tick();
    EXPECT_EQ(fx.Clip(), "asset://anim/walk_crouch.sanim");
    EXPECT_EQ(fx.BehaviorName(fx.Entity), "Anim.Locomotion.Walk");
    // Half-way through the 1.2 s crouch walk, where the walk was.
    EXPECT_NEAR(fx.Playing(fx.Entity).TimeSeconds, 0.6f, 1e-4f);
    const AnimDecisionRecord* changed = fx.LastRecord(fx.Entity, AnimDecisionCause::ContentChanged);
    ASSERT_NE(changed, nullptr);
    EXPECT_EQ(changed->Reason, AnimChangeReason::RowChanged);
    // The winner did not change: content moved under a stable behavior.
    EXPECT_EQ(fx.Selection(fx.Entity).Behavior, fx.Tag("Anim.Locomotion.Walk"));
}

TEST(AnimContent, OneShotContentIsPinnedAtEntry)
{
    Hero fx;
    fx.Motion(fx.Entity).Grounded = false;
    fx.Tick(5);
    fx.Motion(fx.Entity).Grounded = true;
    fx.Tick();
    ASSERT_EQ(fx.Clip(), "asset://anim/land.sanim");
    EXPECT_TRUE(fx.Playing(fx.Entity).Pinned);

    // Crouching now would resolve land_crouch; the landing already playing
    // keeps its clip.
    fx.Motion(fx.Entity).Crouched = true;
    fx.Tick(3);
    EXPECT_EQ(fx.Clip(), "asset://anim/land.sanim");

    // It completes and says so for the next tick's selection.
    fx.Tick(12);
    EXPECT_TRUE(fx.Playing(fx.Entity).ContentComplete);
    EXPECT_FLOAT_EQ(fx.Playing(fx.Entity).TimeSeconds, 0.25f);
}

TEST(AnimContent, APropPlaysRequestsWithNoFactsOrSelector)
{
    AnimRigFixture fx({ "Anim.Door.Closed", "anim.intent.door_open" });
    fx.Clip("asset://anim/closed.sanim", 1.0f);
    fx.Clip("asset://anim/open.sanim", 0.5f);
    (void)fx.Load("asset://anim/door.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Door.Closed", "kind": "hold" }, { "tag": "anim.intent.door_open", "kind": "hold" } ] })");
    (void)fx.Load("asset://anim/door.slots.sdata", kAnimSlotMapType, R"({ "rows": [
        { "behavior": "Anim.Door.Closed", "clip": "asset://anim/closed.sanim" },
        { "behavior": "anim.intent.door_open", "clip": "asset://anim/open.sanim" } ] })");
    const DataAssetHandle rig = fx.Load("asset://anim/door.rig.sdata", kAnimRigType, R"({
        "behaviors": [ "asset://anim/door.behaviors.sdata" ], "slot_maps": [ "asset://anim/door.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Door.Closed" } ] })");
    ASSERT_TRUE(fx.Bound(rig).Valid) << AnimRigFixture::Describe(fx.Bound(rig));

    const EntityId door = fx.Entities.CreateEntity();
    fx.Entities.AddComponent(door, AnimRig{ rig });
    // The tier is what it carries: no facts, no history, no selector state.
    EXPECT_EQ(fx.Entities.TryGet<AnimFacts>(door), nullptr);
    EXPECT_EQ(fx.Entities.TryGet<AnimFactHistory>(door), nullptr);
    EXPECT_EQ(fx.Entities.TryGet<AnimSelectorState>(door), nullptr);
    ASSERT_NE(fx.Entities.TryGet<AnimContentState>(door), nullptr);

    fx.Tick();
    EXPECT_EQ(fx.ClipName(door, fx.Bound(rig)), "asset://anim/closed.sanim");

    const AnimRequestResult open = fx.Issue(door, "anim.intent.door_open");
    ASSERT_TRUE(open.Accepted());
    fx.Tick();
    EXPECT_EQ(fx.ClipName(door, fx.Bound(rig)), "asset://anim/open.sanim");
    fx.Tick(40);
    EXPECT_TRUE(fx.Playing(door).ContentComplete);
    EXPECT_FLOAT_EQ(fx.Playing(door).TimeSeconds, 0.5f);

    ASSERT_TRUE(CancelAnimRequest(fx.Entities, door, open.Id, AnimCancelReason::Released, fx.Now));
    fx.Tick();
    EXPECT_EQ(fx.ClipName(door, fx.Bound(rig)), "asset://anim/closed.sanim");
}

TEST(AnimContent, OverlaysInsertRowsByPriority)
{
    Hero fx;
    fx.Clip("asset://anim/walk_mod.sanim", 1.0f);
    fx.Clip("asset://anim/walk_tie.sanim", 1.0f);
    (void)fx.Load("asset://anim/mod.slots.sdata", kAnimSlotMapType, R"({ "rows": [
        { "behavior": "Anim.Locomotion.Walk", "priority": 0, "clip": "asset://anim/walk_tie.sanim" },
        { "behavior": "Anim.Locomotion.Walk", "priority": 5, "when": [ { "fact": "Crouched", "not": true } ],
          "clip": "asset://anim/walk_mod.sanim" } ] })");
    fx.Reload("asset://anim/hero.rig.sdata", kAnimRigType, R"({
        "facts": "asset://anim/hero.facts.sdata", "requests": "asset://anim/hero.requests.sdata",
        "behaviors": [ "asset://anim/hero.behaviors.sdata" ],
        "slot_maps": [ "asset://anim/hero.slots.sdata", "asset://anim/mod.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "selector": "asset://anim/hero.selector.sdata",
                      "idle": "Anim.Locomotion.Idle" } ] })");

    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Tick();
    // The overlay's priority-5 row shadows the base walk.
    EXPECT_EQ(fx.Clip(), "asset://anim/walk_mod.sanim");

    // Crouched, the overlay row does not match; the base map's rows come
    // before the overlay's equal-priority row.
    fx.Motion(fx.Entity).Crouched = true;
    fx.Tick();
    EXPECT_EQ(fx.Clip(), "asset://anim/walk_crouch.sanim");
}

TEST(AnimContent, RowsReadingLocalFactsMustShareTiming)
{
    AnimRigFixture fx({ "Anim.Idle" });
    fx.Clip("asset://anim/a.sanim", 1.0f);
    fx.Clip("asset://anim/b.sanim", 1.5f);
    (void)fx.Load("asset://anim/l.facts.sdata", kAnimFactSchemaType, R"({
        "slots": [ { "name": "Variant", "kind": "int", "local": true } ] })");
    (void)fx.Load("asset://anim/l.slots.sdata", kAnimSlotMapType, R"({ "rows": [
        { "behavior": "Anim.Idle", "when": [ { "fact": "Variant", "compare": "eq", "value": 1 } ],
          "clip": "asset://anim/a.sanim" },
        { "behavior": "Anim.Idle", "clip": "asset://anim/b.sanim" } ] })");
    const DataAssetHandle rig = fx.Load("asset://anim/l.rig.sdata", kAnimRigType, R"({
        "facts": "asset://anim/l.facts.sdata", "slot_maps": [ "asset://anim/l.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" } ] })");

    const AnimBoundRig& bound = fx.Bound(rig);
    const AnimDiagnostic* timing = AnimRigFixture::FindCode(bound, "anim.slot.local_timing");
    ASSERT_NE(timing, nullptr) << AnimRigFixture::Describe(bound);
    EXPECT_EQ(timing->AssetPath, "asset://anim/l.slots.sdata");
    EXPECT_EQ(timing->FieldPath, "$.data.rows[0].when");
}

TEST(AnimContent, PinnedContentWhoseRowIsRemovedReanchors)
{
    Hero fx;
    fx.Motion(fx.Entity).Grounded = false;
    fx.Tick(5);
    fx.Motion(fx.Entity).Grounded = true;
    fx.Tick();
    ASSERT_EQ(fx.Clip(), "asset://anim/land.sanim");

    fx.Reload("asset://anim/hero.slots.sdata", kAnimSlotMapType, R"({ "rows": [
        { "behavior": "Anim.Locomotion.Idle", "clip": "asset://anim/idle.sanim" },
        { "behavior": "Anim.Action.Land", "clip": "asset://anim/land_crouch.sanim" } ] })");
    fx.Tick();
    EXPECT_EQ(fx.Clip(), "asset://anim/land_crouch.sanim");
    const AnimDecisionRecord* anchored = fx.LastRecord(fx.Entity, AnimDecisionCause::Anchored);
    ASSERT_NE(anchored, nullptr);
    EXPECT_EQ(anchored->Reason, AnimChangeReason::Rebound);
}
