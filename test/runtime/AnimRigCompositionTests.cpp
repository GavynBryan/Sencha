// An entity names its rig; the rig's binding decides every other animation
// component the entity carries, and one system provisions them.

#include "AnimFlowFixture.h"
#include "AnimRigFixture.h"

#include <anim/AnimPoseState.h>
#include <anim/AnimRigCompositionSystem.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <world/ComponentRegistrar.h>

#include <gtest/gtest.h>

namespace
{
    template <typename T>
    bool Carries(const AnimRigFixture& fx, EntityId entity)
    {
        return fx.Entities.HasComponent<T>(entity);
    }

    // One request-keyed layer playing a clip keyed to a one-joint skeleton: no facts,
    // no selector, no flow.
    DataAssetHandle LoadPropRig(AnimRigFixture& fx)
    {
        (void)fx.Tags().RegisterTag("Anim.Door.Open");
        SkeletonData skeleton;
        skeleton.Joints.push_back(SkeletonJoint{ .Name = "hinge", .ParentIndex = -1 });
        (void)fx.Skeletons.Register("asset://anim/door.sskel", std::move(skeleton));
        fx.Clip("asset://anim/door.sanim", 1.0f, {}, "asset://anim/door.sskel");
        (void)fx.Load("asset://anim/door.requests.sdata", kAnimRequestSchemaType,
                      R"({ "intents": [ { "intent": "Anim.Door.Open", "params": [] } ] })");
        (void)fx.Load("asset://anim/door.behaviors.sdata", kAnimBehaviorSetType,
                      R"({ "behaviors": [ { "tag": "Anim.Door.Open", "kind": "one_shot" } ] })");
        (void)fx.Load("asset://anim/door.slots.sdata", kAnimSlotMapType,
                      R"({ "rows": [ { "id": "open", "behavior": "Anim.Door.Open", "clip": "asset://anim/door.sanim" } ] })");
        const DataAssetHandle rig = fx.Load("asset://anim/door.rig.sdata", kAnimRigType, R"({
            "skeleton": "asset://anim/door.sskel", "requests": "asset://anim/door.requests.sdata",
            "behaviors": [ "asset://anim/door.behaviors.sdata" ], "slot_maps": [ "asset://anim/door.slots.sdata" ],
            "layers": [ { "name": "anim.layer.base" } ] })");
        EXPECT_TRUE(fx.Bound(rig).Valid) << AnimRigFixture::Describe(fx.Bound(rig));
        return rig;
    }
}

TEST(AnimRigComposition, APropRigBringsOnlyWhatEveryRigHas)
{
    AnimRigFixture fx;
    const EntityId door = fx.Character(LoadPropRig(fx));
    fx.Tick();

    EXPECT_TRUE(Carries<AnimRequestSet>(fx, door));
    EXPECT_TRUE(Carries<AnimContentState>(fx, door));
    EXPECT_FALSE(Carries<AnimFacts>(fx, door));
    EXPECT_FALSE(Carries<AnimFactsLarge>(fx, door));
    EXPECT_FALSE(Carries<AnimFactHistory>(fx, door));
    EXPECT_FALSE(Carries<AnimSelectorState>(fx, door));
    EXPECT_FALSE(Carries<AnimFlowState>(fx, door));
    EXPECT_FALSE(Carries<AnimPoseState>(fx, door)) << "nothing draws it";
}

TEST(AnimRigComposition, ACharacterRigBringsItsFactsHistoryAndSelection)
{
    AnimRigFixture fx;
    AnimCharacterRig::RegisterTags(fx);
    const EntityId walker = fx.Character(AnimCharacterRig::Load(fx), { .Speed = 1.0f });
    fx.Tick();

    EXPECT_TRUE(Carries<AnimFacts>(fx, walker));
    EXPECT_FALSE(Carries<AnimFactsLarge>(fx, walker));
    EXPECT_TRUE(Carries<AnimFactHistory>(fx, walker)) << "an edge remembers the last tick";
    EXPECT_TRUE(Carries<AnimSelectorState>(fx, walker));
    EXPECT_FALSE(Carries<AnimFlowState>(fx, walker));
    EXPECT_EQ(fx.BehaviorName(walker), "Anim.Locomotion.Walk") << "selected on the tick it was composed";
}

TEST(AnimRigComposition, AFlowRigBringsFlowState)
{
    ReloadFlowFixture fx(CountedLoopFlow());
    fx.Tick();
    EXPECT_TRUE(Carries<AnimFlowState>(fx, fx.Entity));
    EXPECT_FALSE(Carries<AnimSelectorState>(fx, fx.Entity)) << "its layer is keyed by requests";
    EXPECT_FALSE(Carries<AnimFactHistory>(fx, fx.Entity)) << "no derivation remembers anything";
}

TEST(AnimRigComposition, PoseStateNeedsAConsumerAndAMachineThatPresents)
{
    AnimRigFixture fx;
    const DataAssetHandle rig = LoadPropRig(fx);
    const EntityId drawn = fx.DrawnCharacter(rig);
    const EntityId undrawn = fx.Character(rig);
    fx.Tick();
    EXPECT_TRUE(Carries<AnimPoseState>(fx, drawn));
    EXPECT_FALSE(Carries<AnimPoseState>(fx, undrawn));

    AnimRigFixture headless;
    const EntityId served = headless.DrawnCharacter(LoadPropRig(headless));
    AnimRigCompositionSystem composition(false);
    composition.Compose(headless.Entities);
    EXPECT_FALSE(Carries<AnimPoseState>(headless, served));
}

TEST(AnimRigComposition, ASkinnedMeshIsAPoseConsumer)
{
    AnimRigFixture fx;
    fx.Entities.RegisterComponent<SkinnedMeshComponent>();
    const EntityId entity = fx.Entities.CreateEntity();
    fx.Entities.AddComponent(entity, SkinnedMeshComponent{});
    EXPECT_TRUE(Carries<AnimPoseConsumer>(fx, entity));
}

// What an entity carries follows its rig across reloads: storage the rig
// outgrows is replaced by what it asks for now.
TEST(AnimRigComposition, AReloadRecomposesWhatTheRigNeeds)
{
    AnimRigFixture fx;
    AnimCharacterRig::RegisterTags(fx);
    const DataAssetHandle rig = AnimCharacterRig::Load(fx);
    const EntityId walker = fx.Character(rig);
    fx.Tick();
    ASSERT_TRUE(Carries<AnimFacts>(fx, walker));

    std::string large(AnimCharacterRig::kRig);
    large.insert(large.find('{') + 1, R"( "fact_capacity": "large",)");
    fx.Reload("asset://anim/character.rig.sdata", kAnimRigType, large);
    fx.Tick();
    EXPECT_FALSE(Carries<AnimFacts>(fx, walker));
    EXPECT_TRUE(Carries<AnimFactsLarge>(fx, walker));
    EXPECT_TRUE(Carries<AnimSelectorState>(fx, walker));
}

// Storage put on an entity by hand is still the rig's to decide once composed.
TEST(AnimRigComposition, TheRigDecidesOverStoragePutThereByHand)
{
    AnimRigFixture fx;
    AnimCharacterRig::RegisterTags(fx);
    const EntityId walker = fx.Character(AnimCharacterRig::Load(fx));
    fx.Entities.AddComponent(walker, AnimFactsLarge{});
    fx.Entities.AddComponent(walker, AnimFlowState{});
    fx.Tick();
    EXPECT_TRUE(Carries<AnimFacts>(fx, walker));
    EXPECT_FALSE(Carries<AnimFactsLarge>(fx, walker));
    EXPECT_FALSE(Carries<AnimFlowState>(fx, walker));
}

TEST(AnimRigComposition, AnEntityThatLosesItsRigLosesWhatTheRigBrought)
{
    AnimRigFixture fx;
    AnimCharacterRig::RegisterTags(fx);
    const EntityId walker = fx.Character(AnimCharacterRig::Load(fx));
    fx.Tick();
    ASSERT_TRUE(Carries<AnimSelectorState>(fx, walker));

    fx.Entities.RemoveComponent<AnimRig>(walker);
    fx.Tick();
    EXPECT_FALSE(Carries<AnimFacts>(fx, walker));
    EXPECT_FALSE(Carries<AnimFactHistory>(fx, walker));
    EXPECT_FALSE(Carries<AnimSelectorState>(fx, walker));
    EXPECT_FALSE(Carries<AnimRigComposition>(fx, walker));
}

// A rig still loading or failing to bind says nothing about what it needs, so an
// entity keeps what it has until one does.
TEST(AnimRigComposition, AnInvalidRigLeavesTheEntityAsItIs)
{
    AnimRigFixture fx;
    AnimCharacterRig::RegisterTags(fx);
    const DataAssetHandle rig = AnimCharacterRig::Load(fx);
    const EntityId walker = fx.Character(rig);
    fx.Tick();
    ASSERT_TRUE(Carries<AnimFacts>(fx, walker));

    // A layer named by nothing the World declares fails the binding.
    std::string broken(AnimCharacterRig::kRig);
    broken.replace(broken.find("anim.layer.base"), std::string_view("anim.layer.base").size(), "anim.layer.undeclared");
    fx.Reload("asset://anim/character.rig.sdata", kAnimRigType, broken);
    ASSERT_FALSE(fx.Bound(rig).Valid);
    fx.Tick();
    EXPECT_TRUE(Carries<AnimFacts>(fx, walker));
    EXPECT_TRUE(Carries<AnimSelectorState>(fx, walker));
}
