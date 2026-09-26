// A machine that presents no pose runs animation only for rigs whose binding cannot
// rule out reaching gameplay.

#include "AnimFlowFixture.h"
#include "AnimRigFixture.h"

#include <anim/AnimDecisionLog.h>
#include <anim/AnimEventSystem.h>

namespace
{
    struct HeadlessCharacter : AnimRigFixture
    {
        DataAssetHandle Rig;
        EntityId Entity;
        AnimFactGatherSystem HeadlessGather{ nullptr, false };
        AnimSelectSystem HeadlessSelect{ false };
        AnimContentSystem HeadlessContent{ false };
        AnimEventSystem HeadlessEvents{ nullptr, false };

        HeadlessCharacter()
        {
            AnimCharacterRig::RegisterTags(*this);
            Rig = AnimCharacterRig::Load(*this);
            Entity = Character(Rig, { .Speed = 1.0f });
        }

        void TickHeadless(int count)
        {
            for (int i = 0; i < count; ++i, ++Now)
            {
                HeadlessGather.Gather(Entities, Now, kTick);
                HeadlessSelect.Select(Entities, Now, kTick);
                HeadlessContent.Resolve(Entities, Now, kTick);
                HeadlessEvents.Run(Entities, Now, kTick);
            }
        }

        void ReplaceIdleClip(std::vector<AnimationClipEvent> events)
        {
            AnimationClipData clip = *Clips.Get(Clips.Find("asset://anim/idle.sanim"));
            clip.Events = std::move(events);
            ASSERT_TRUE(Clips.ReloadInPlace(Clips.Find("asset://anim/idle.sanim"), std::move(clip)));
        }

        const AnimBoundRig& Bound() { return AnimRigFixture::Bound(Rig); }
    };

    AnimationClipEvent GameplayEvent()
    {
        AnimationClipEvent event;
        event.Key = 1;
        event.Time = 0.1f;
        event.Binding = "test.gameplay_mark";
        event.Scope = AnimEventScope::Gameplay;
        return event;
    }
}

TEST(AnimServerParticipation, AHeadlessMachineLeavesACosmeticRigAlone)
{
    HeadlessCharacter headless;
    ASSERT_FALSE(headless.Bound().DrivesGameplay);
    headless.TickHeadless(10);
    EXPECT_FALSE(headless.Playing(headless.Entity).Behavior.IsValid());
    EXPECT_EQ(headless.Entities.TryGet<AnimFacts>(headless.Entity)->Values[0], 0u) << "facts not gathered";
    EXPECT_EQ(headless.Log(headless.Entity).Written, 0u);

    HeadlessCharacter presenting;
    presenting.Tick(10);
    EXPECT_EQ(presenting.BehaviorName(presenting.Entity), "Anim.Locomotion.Walk");
    EXPECT_GT(presenting.Log(presenting.Entity).Written, 0u);
}

TEST(AnimServerParticipation, AHeadlessAuthorityStillFiresGameplayEvents)
{
    HeadlessCharacter headless;
    headless.Motion(headless.Entity).Speed = 0.0f;
    headless.ReplaceIdleClip({ GameplayEvent() });
    ASSERT_TRUE(headless.Bound().DrivesGameplay);
    headless.TickHeadless(20);
    EXPECT_EQ(headless.BehaviorName(headless.Entity), "Anim.Locomotion.Idle");
    EXPECT_NE(headless.LastRecord(headless.Entity, AnimDecisionCause::EventCrossed), nullptr);
}

// Whether a rig drives gameplay follows its content through every rebind.
TEST(AnimServerParticipation, GameplayReachFollowsContentAcrossRebinds)
{
    HeadlessCharacter fx;
    ASSERT_FALSE(fx.Bound().DrivesGameplay);

    fx.ReplaceIdleClip({ GameplayEvent() });
    EXPECT_TRUE(fx.Bound().DrivesGameplay) << "a gameplay clip event";
    fx.TickHeadless(2);
    EXPECT_TRUE(fx.Playing(fx.Entity).Behavior.IsValid()) << "and the headless machine now runs it";
    fx.ReplaceIdleClip({});
    EXPECT_FALSE(fx.Bound().DrivesGameplay);

    std::string rootMotion(AnimCharacterRig::kBehaviors);
    rootMotion.replace(rootMotion.find(R"("kind": "hold")"), 14, R"("kind": "hold", "root_motion": true)");
    fx.Reload("asset://anim/character.behaviors.sdata", kAnimBehaviorSetType, rootMotion);
    EXPECT_TRUE(fx.Bound().DrivesGameplay) << "a root-motion behavior";
    fx.Reload("asset://anim/character.behaviors.sdata", kAnimBehaviorSetType, AnimCharacterRig::kBehaviors);
    EXPECT_FALSE(fx.Bound().DrivesGameplay);

    std::string gameplayLifecycle(AnimCharacterRig::kBehaviors);
    gameplayLifecycle.replace(gameplayLifecycle.find(R"("kind": "hold")"), 14,
                              R"("kind": "hold", "on_entered": { "binding": "test.died", "scope": "gameplay" })");
    fx.Reload("asset://anim/character.behaviors.sdata", kAnimBehaviorSetType, gameplayLifecycle);
    EXPECT_TRUE(fx.Bound().DrivesGameplay) << "a gameplay lifecycle binding";
}

TEST(AnimServerParticipation, WhatBindingCannotSeeIntoCountsAsGameplay)
{
    ReloadFlowFixture flow(CountedLoopFlow());
    EXPECT_TRUE(flow.Bound(flow.Rig).DrivesGameplay) << "flows carry anchors the authority stamps";

    HeadlessCharacter missing;
    missing.Reload("asset://anim/character.slots.sdata", kAnimSlotMapType, R"({ "rows": [
        { "behavior": "Anim.Locomotion.Idle", "clip": "asset://anim/not_loaded.sanim" } ] })");
    EXPECT_TRUE(missing.Bound().DrivesGameplay) << "a clip whose events are unknown";
}
