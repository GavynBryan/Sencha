// Content resolution applies a behavior by kind every tick: cyclic content keeps
// normalized time across row changes, one-shot content is pinned at entry, and a
// request-keyed layer needs no facts or selector.

#include "AnimRigFixture.h"

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
            EXPECT_TRUE(Bound(Rig).Valid) << Describe(Bound(Rig));
            Entity = Character(Rig);
        }
        std::string Clip() { return ClipName(Entity, Bound(Rig)); }
    };
}

TEST(AnimContent, ARowChangeUnderACyclicBehaviorCarriesNormalizedTime)
{
    CharacterRigFixture fx;
    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Tick(31);
    ASSERT_EQ(fx.Clip(), "asset://anim/walk.sanim");
    // Walk won on tick 0; thirty ticks in, the 1 s clip is at 0.5.
    EXPECT_NEAR(fx.Playing(fx.Entity).TimeSeconds, 0.5f, 1e-4f);

    fx.Motion(fx.Entity).Crouched = true;
    fx.Tick();
    EXPECT_EQ(fx.Clip(), "asset://anim/walk_crouch.sanim");
    EXPECT_EQ(fx.BehaviorName(fx.Entity), "Anim.Locomotion.Walk");
    // On this tick the walk is 31 of its 60 ticks in; the 1.2 s crouch walk
    // takes over at that same fraction, so neither loses a tick.
    EXPECT_NEAR(fx.Playing(fx.Entity).TimeSeconds, 31.0f / 60.0f * 1.2f, 1e-4f);
    const AnimDecisionRecord* changed = fx.LastRecord(fx.Entity, AnimDecisionCause::ContentChanged);
    ASSERT_NE(changed, nullptr);
    EXPECT_EQ(changed->Reason, AnimChangeReason::RowChanged);
    // The winner did not change: content moved under a stable behavior.
    EXPECT_EQ(fx.Selection(fx.Entity).Behavior, fx.Tag("Anim.Locomotion.Walk"));
}

TEST(AnimContent, OneShotContentIsPinnedAtEntry)
{
    CharacterRigFixture fx;
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
        { "id": "closed", "behavior": "Anim.Door.Closed", "clip": "asset://anim/closed.sanim" },
        { "id": "door_open", "behavior": "anim.intent.door_open", "clip": "asset://anim/open.sanim" } ] })");
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
    CharacterRigFixture fx;
    fx.Clip("asset://anim/walk_mod.sanim", 1.0f);
    fx.Clip("asset://anim/walk_tie.sanim", 1.0f);
    (void)fx.Load("asset://anim/mod.slots.sdata", kAnimSlotMapType, R"({ "rows": [
        { "id": "walk", "behavior": "Anim.Locomotion.Walk", "priority": 0, "clip": "asset://anim/walk_tie.sanim" },
        { "id": "walk_2", "behavior": "Anim.Locomotion.Walk", "priority": 5, "when": [ { "fact": "Crouched", "not": true } ],
          "clip": "asset://anim/walk_mod.sanim" } ] })");
    fx.Reload("asset://anim/character.rig.sdata", kAnimRigType, R"({
        "facts": "asset://anim/character.facts.sdata", "requests": "asset://anim/character.requests.sdata",
        "behaviors": [ "asset://anim/character.behaviors.sdata" ],
        "slot_maps": [ "asset://anim/character.slots.sdata", "asset://anim/mod.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "selector": "asset://anim/character.selector.sdata",
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
        { "id": "idle", "behavior": "Anim.Idle", "when": [ { "fact": "Variant", "compare": "eq", "value": 1 } ],
          "clip": "asset://anim/a.sanim" },
        { "id": "idle_2", "behavior": "Anim.Idle", "clip": "asset://anim/b.sanim" } ] })");
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
    CharacterRigFixture fx;
    fx.Motion(fx.Entity).Grounded = false;
    fx.Tick(5);
    fx.Motion(fx.Entity).Grounded = true;
    fx.Tick();
    ASSERT_EQ(fx.Clip(), "asset://anim/land.sanim");

    fx.Reload("asset://anim/character.slots.sdata", kAnimSlotMapType, R"({ "rows": [
        { "id": "idle", "behavior": "Anim.Locomotion.Idle", "clip": "asset://anim/idle.sanim" },
        { "id": "land", "behavior": "Anim.Action.Land", "clip": "asset://anim/land_crouch.sanim" } ] })");
    fx.Tick();
    EXPECT_EQ(fx.Clip(), "asset://anim/land_crouch.sanim");
    const AnimDecisionRecord* reset = fx.LastRecord(fx.Entity, AnimDecisionCause::IndexReset);
    ASSERT_NE(reset, nullptr);
    EXPECT_EQ(reset->Reason, AnimChangeReason::Rebound);
}

// A behavior that carries phase within its sync group starts where the one it
// replaces had reached, in normalized time; without carry it starts over.
TEST(AnimContent, PhaseCarriesWithinASyncGroup)
{
    CharacterRigFixture fx;
    (void)fx.Tags().RegisterTag("Anim.Sync.Locomotion");
    fx.Reload("asset://anim/character.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Locomotion.Idle", "kind": "cyclic" },
        { "tag": "Anim.Locomotion.Walk", "kind": "cyclic", "sync_group": "Anim.Sync.Locomotion" },
        { "tag": "Anim.Locomotion.Sprint", "kind": "cyclic", "sync_group": "Anim.Sync.Locomotion",
          "blend": { "in": "crossfade", "in_ms": 200, "phase": "carry" } },
        { "tag": "Anim.Action.Land", "kind": "one_shot" },
        { "tag": "Anim.Action.Reload", "kind": "one_shot",
          "latch": { "mode": "until_request_ends", "interruptible_by": "tags", "tags": [ "Anim.Death" ] } },
        { "tag": "Anim.Death", "kind": "hold" } ] })");
    ASSERT_TRUE(fx.Bound(fx.Rig).Valid) << AnimRigFixture::Describe(fx.Bound(fx.Rig));

    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Tick(30);
    ASSERT_EQ(fx.Clip(), "asset://anim/walk.sanim");
    fx.Motion(fx.Entity).Speed = 3.0f;
    fx.Tick();
    ASSERT_EQ(fx.Clip(), "asset://anim/sprint.sanim");
    // Walk (1 s) was 30 ticks in on this tick: half way. Sprint (0.8 s)
    // continues from its own half way.
    EXPECT_NEAR(fx.Playing(fx.Entity).TimeSeconds, 0.4f, 1e-4f);

    // Back to walk, which does not carry: it starts over.
    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Tick();
    ASSERT_EQ(fx.Clip(), "asset://anim/walk.sanim");
    EXPECT_FLOAT_EQ(fx.Playing(fx.Entity).TimeSeconds, 0.0f);
}

// A cancel that reaches this machine late -- as a client hears it, after the
// authority made it -- still ends the request at the tick it was made: what
// plays next starts there, so it runs in step with the authority's.
TEST(AnimContent, ContentAfterALateCancelStartsAtTheCancel)
{
    CharacterRigFixture fx;
    fx.Reload("asset://anim/character.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Locomotion.Idle", "kind": "cyclic" },
        { "tag": "Anim.Locomotion.Walk", "kind": "cyclic" },
        { "tag": "Anim.Locomotion.Sprint", "kind": "cyclic" },
        { "tag": "Anim.Action.Land", "kind": "one_shot" },
        { "tag": "Anim.Action.Reload", "kind": "one_shot",
          "latch": { "mode": "until_request_ends", "interruptible_by": "never", "on_request_cancel": "abort" } },
        { "tag": "Anim.Death", "kind": "hold" } ] })");
    fx.Tick();
    const AnimRequestResult reload = fx.Issue(fx.Entity, "anim.intent.reload");
    fx.Tick(10);
    ASSERT_EQ(fx.BehaviorName(fx.Entity), "Anim.Action.Reload");

    const AnimTick cancelled = fx.Now - 2;
    ASSERT_TRUE(CancelAnimRequest(*fx.Entities.TryGet<AnimRequestSet>(fx.Entity), reload.Id,
                                  AnimCancelReason::Released, cancelled, nullptr));
    fx.Tick();
    EXPECT_EQ(fx.BehaviorName(fx.Entity), "Anim.Locomotion.Idle");
    EXPECT_EQ(fx.Playing(fx.Entity).StartTick, cancelled);
    EXPECT_NEAR(fx.Playing(fx.Entity).TimeSeconds, 2.0f / 60.0f, 1e-5f);
}

// A reload that finishes its clip after the cancel hands over when the clip
// ends, which every machine works out from the request's start alike.
TEST(AnimContent, ContentAfterAFinishedReloadStartsWhenItFinished)
{
    CharacterRigFixture fx;
    fx.Tick();
    const AnimRequestResult reload = fx.Issue(fx.Entity, "anim.intent.reload");
    fx.Tick(10);
    ASSERT_EQ(fx.BehaviorName(fx.Entity), "Anim.Action.Reload");
    ASSERT_TRUE(CancelAnimRequest(*fx.Entities.TryGet<AnimRequestSet>(fx.Entity), reload.Id,
                                  AnimCancelReason::Released, fx.Now - 2, nullptr));
    while (fx.BehaviorName(fx.Entity) == "Anim.Action.Reload" && fx.Now < 200)
        fx.Tick();
    EXPECT_EQ(fx.BehaviorName(fx.Entity), "Anim.Locomotion.Idle");
    EXPECT_EQ(fx.Playing(fx.Entity).StartTick, fx.Last()) << "not the cancel tick";
}
