// Layers: each composes into the pose at a weight its selector's weight rules
// choose from this tick's facts and requests, or at the rig's constant, over
// the joints its bone mask names in the rig's skeleton.

#include "AnimRigFixture.h"

#include <anim/AnimFacts.h>

#include <gtest/gtest.h>

#include <format>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr const char* kLayerTags[] = {
        "anim.intent.reload", "anim.layer.upper", "Anim.Idle", "Anim.Upper.None", "Anim.Upper.Reload",
    };

    // An upper layer that plays the reload and is weighted by rules: out
    // entirely while standing still with a reload (the base layer plays the
    // full-body variant then), and by Speed while crouched.
    constexpr std::string_view kUpperSelector = R"({ "rules": [
        { "name": "reload", "priority": 50, "enter": [ { "request": "anim.intent.reload" } ],
          "behavior": "Anim.Upper.Reload" },
        { "name": "none", "priority": 0, "enter": [], "behavior": "Anim.Upper.None" },
        { "name": "covered", "priority": 100, "weight": 0,
          "enter": [ { "request": "anim.intent.reload" }, { "fact": "Speed", "compare": "lt", "value": 0.1 } ] },
        { "name": "aim", "priority": 10, "weight_fact": "Speed", "enter": [ { "fact": "Crouched" } ] } ] })";

    struct LayerFixture : AnimRigFixture
    {
        DataAssetHandle Rig;
        EntityId Entity;

        explicit LayerFixture(std::string_view upperSelector = kUpperSelector)
        {
            for (const char* tag : kLayerTags)
                (void)Tags().RegisterTag(tag);
            Clip("asset://anim/idle.sanim", 1.0f);
            Clip("asset://anim/reload.sanim", 1.0f);
            (void)Load("asset://anim/l.facts.sdata", kAnimFactSchemaType, R"({
                "slots": [ { "name": "Speed", "kind": "float" }, { "name": "Crouched", "kind": "bool" } ] })");
            (void)Load("asset://anim/l.requests.sdata", kAnimRequestSchemaType,
                       R"({ "intents": [ { "intent": "anim.intent.reload", "params": [] } ] })");
            (void)Load("asset://anim/l.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
                { "tag": "Anim.Idle", "kind": "cyclic" }, { "tag": "Anim.Upper.None", "kind": "cyclic" },
                { "tag": "Anim.Upper.Reload", "kind": "one_shot" } ] })");
            (void)Load("asset://anim/l.slots.sdata", kAnimSlotMapType, R"({ "rows": [
                { "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" },
                { "behavior": "Anim.Upper.None", "clip": "asset://anim/idle.sanim" },
                { "behavior": "Anim.Upper.Reload", "clip": "asset://anim/reload.sanim" } ] })");
            (void)Load("asset://anim/l.base.sdata", kAnimSelectorType, R"({ "rules": [
                { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Idle" } ] })");
            (void)Load("asset://anim/l.upper.sdata", kAnimSelectorType, upperSelector);
            Rig = Load("asset://anim/l.rig.sdata", kAnimRigType, R"({
                "facts": "asset://anim/l.facts.sdata", "requests": "asset://anim/l.requests.sdata",
                "behaviors": [ "asset://anim/l.behaviors.sdata" ], "slot_maps": [ "asset://anim/l.slots.sdata" ],
                "layers": [ { "name": "anim.layer.base", "selector": "asset://anim/l.base.sdata", "idle": "Anim.Idle" },
                            { "name": "anim.layer.upper", "selector": "asset://anim/l.upper.sdata",
                              "idle": "Anim.Upper.None", "weight": 0.8 } ] })");
            Entity = Character(Rig);
        }

        float Weight()
        {
            const World& reader = Entities;
            return AnimLayerWeight(Bound(Rig), 1, reader.TryGet<AnimSelectorState>(Entity));
        }
    };
}

TEST(AnimLayerWeight, TheFirstWeightRuleToPassWeightsTheLayer)
{
    LayerFixture fx;
    ASSERT_TRUE(fx.Bound(fx.Rig).Valid) << AnimRigFixture::Describe(fx.Bound(fx.Rig));
    const AnimBoundSelector& upper = fx.Bound(fx.Rig).Selectors[1];
    ASSERT_EQ(upper.WeightRules.size(), 2u);
    EXPECT_EQ(upper.WeightRules[0].Label, "covered");
    // Weight rules are not behavior rules: the upper layer still selects from
    // its two.
    EXPECT_EQ(upper.Rules.size(), 2u);

    fx.Tick();
    EXPECT_FLOAT_EQ(fx.Weight(), 0.8f) << "no weight rule passes: the rig's constant";
    EXPECT_EQ(fx.Selection(fx.Entity, 1).WeightRule, kAnimNoRule);

    fx.Motion(fx.Entity).Crouched = true;
    fx.Motion(fx.Entity).Speed = 0.3f;
    fx.Tick();
    EXPECT_FLOAT_EQ(fx.Weight(), 0.3f);
    fx.Motion(fx.Entity).Speed = 4.0f;
    fx.Tick();
    EXPECT_FLOAT_EQ(fx.Weight(), 1.0f) << "a fact weight is clamped";

    fx.Motion(fx.Entity).Crouched = false;
    fx.Motion(fx.Entity).Speed = 0.0f;
    ASSERT_TRUE(fx.Issue(fx.Entity, "anim.intent.reload").Accepted());
    fx.Tick();
    EXPECT_EQ(fx.BehaviorName(fx.Entity, 1), "Anim.Upper.Reload");
    EXPECT_FLOAT_EQ(fx.Weight(), 0.0f) << "standing still, the base layer covers the reload";
    const AnimDecisionRecord* weighted = fx.LastRecord(fx.Entity, AnimDecisionCause::WeightChanged);
    ASSERT_NE(weighted, nullptr);
    EXPECT_EQ(weighted->Layer, 1u);
    EXPECT_EQ(weighted->Rule, 0u);

    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Tick();
    EXPECT_FLOAT_EQ(fx.Weight(), 0.8f);
    EXPECT_EQ(fx.LastRecord(fx.Entity, AnimDecisionCause::WeightChanged)->Rule, kAnimNoRule);
}

TEST(AnimLayerWeight, AWeightRuleIsChosenByItsEnterAlone)
{
    AnimRigFixture fx;
    EXPECT_NE(fx.CompileError(kAnimSelectorType, R"({ "rules": [
        { "priority": 1, "enter": [], "stay": [], "weight": 0.5 } ] })")
                  .find("no stay, hold or cooldown"),
              std::string::npos);
    EXPECT_NE(fx.CompileError(kAnimSelectorType, R"({ "rules": [
        { "priority": 1, "enter": [], "weight": 0.5, "behavior": "Anim.Idle" } ] })")
                  .find("exactly one of"),
              std::string::npos);
    EXPECT_NE(fx.CompileError(kAnimSelectorType, R"({ "rules": [ { "priority": 1, "enter": [], "weight": 1.5 } ] })")
                  .find("$.data.rules[0].weight"),
              std::string::npos);
}

TEST(AnimLayerWeight, AWeightFactIsAFloatFact)
{
    LayerFixture fx(R"({ "rules": [
        { "name": "none", "priority": 0, "enter": [], "behavior": "Anim.Upper.None" },
        { "name": "aim", "priority": 10, "weight_fact": "Crouched", "enter": [] } ] })");
    const AnimDiagnostic* fact = AnimRigFixture::FindCode(fx.Bound(fx.Rig), "anim.selector.weight_fact");
    ASSERT_NE(fact, nullptr) << AnimRigFixture::Describe(fx.Bound(fx.Rig));
    EXPECT_EQ(fact->AssetPath, "asset://anim/l.upper.sdata");
    EXPECT_EQ(fact->FieldPath, "$.data.rules[1].weight_fact");
}

namespace
{
    SkeletonData MakeSkeleton(std::initializer_list<std::pair<const char*, int>> joints)
    {
        SkeletonData skeleton;
        for (const auto& [name, parent] : joints)
        {
            SkeletonJoint joint;
            joint.Name = name;
            joint.ParentIndex = parent;
            skeleton.Joints.push_back(joint);
        }
        return skeleton;
    }

    // root > pelvis > spine > neck > head; spine > arm_l > hand_l; pelvis > leg_l.
    SkeletonData Biped()
    {
        return MakeSkeleton({ { "root", -1 }, { "pelvis", 0 }, { "spine", 1 }, { "neck", 2 }, { "head", 3 },
                              { "arm_l", 2 }, { "hand_l", 5 }, { "leg_l", 1 } });
    }

    constexpr const char* kSkeleton = "asset://anim/biped.sskel";

    // A two-layer prop rig on the biped whose upper layer carries `mask`.
    const AnimBoundRig& BindMasked(AnimRigFixture& fx, std::string_view mask, std::string_view baseMask = {},
                                   std::string_view skeleton = kSkeleton)
    {
        (void)fx.Tags().RegisterTag("Anim.Idle");
        (void)fx.Tags().RegisterTag("anim.layer.upper");
        fx.Clip("asset://anim/idle.sanim", 1.0f, {}, kSkeleton);
        (void)fx.Load("asset://anim/m.behaviors.sdata", kAnimBehaviorSetType,
                      R"({ "behaviors": [ { "tag": "Anim.Idle", "kind": "cyclic" } ] })");
        (void)fx.Load("asset://anim/m.slots.sdata", kAnimSlotMapType,
                      R"({ "rows": [ { "behavior": "Anim.Idle", "clip": "asset://anim/idle.sanim" } ] })");
        const std::string skeletonField =
            skeleton.empty() ? std::string() : std::format(R"("skeleton": "{}",)", skeleton);
        const std::string base = baseMask.empty() ? std::string() : std::format(R"(, "mask": {})", baseMask);
        return fx.Bound(fx.Load("asset://anim/m.rig.sdata", kAnimRigType,
                                std::format(R"({{ {}
            "behaviors": [ "asset://anim/m.behaviors.sdata" ], "slot_maps": [ "asset://anim/m.slots.sdata" ],
            "layers": [ {{ "name": "anim.layer.base", "idle": "Anim.Idle" {} }},
                        {{ "name": "anim.layer.upper", "idle": "Anim.Idle", "mask": {} }} ] }})",
                                            skeletonField, base, mask)));
    }
}

// Steps apply in order to an empty set, each to a joint and, unless it says
// otherwise, everything below it.
TEST(AnimLayerMask, StepsIncludeAndExcludeSubtreesByJointName)
{
    AnimRigFixture fx;
    (void)fx.Skeletons.Register(kSkeleton, Biped());
    const AnimBoundRig& rig = BindMasked(fx, R"([ { "joint": "spine" },
        { "joint": "neck", "exclude": true, "subtree": false }, { "joint": "leg_l", "subtree": false } ])");
    ASSERT_TRUE(rig.Valid) << AnimRigFixture::Describe(rig);
    EXPECT_EQ(rig.JointCount, 8u);
    EXPECT_FALSE(rig.Layers[0].Masked());
    const std::vector<std::uint8_t> expected{ 0, 0, 1, 0, 1, 1, 1, 1 };
    EXPECT_EQ(rig.Layers[1].Mask, expected);
    EXPECT_TRUE(rig.Layers[0].Covers(0));
    EXPECT_FALSE(rig.Layers[1].Covers(3));
}

TEST(AnimLayerMask, AnUnknownJointIsALocatedError)
{
    AnimRigFixture fx;
    (void)fx.Skeletons.Register(kSkeleton, Biped());
    const AnimBoundRig& rig = BindMasked(fx, R"([ { "joint": "spine" }, { "joint": "tail" } ])");
    const AnimDiagnostic* unknown = AnimRigFixture::FindCode(rig, "anim.mask.joint_unknown");
    ASSERT_NE(unknown, nullptr) << AnimRigFixture::Describe(rig);
    EXPECT_EQ(unknown->AssetPath, "asset://anim/m.rig.sdata");
    EXPECT_EQ(unknown->FieldPath, "$.data.layers[1].mask[1].joint");
    EXPECT_FALSE(rig.Valid);
}

TEST(AnimLayerMask, AnAmbiguousJointCannotBeNamed)
{
    AnimRigFixture fx;
    (void)fx.Skeletons.Register(kSkeleton, MakeSkeleton({ { "root", -1 }, { "bone", 0 }, { "bone", 0 } }));
    const AnimBoundRig& rig = BindMasked(fx, R"([ { "joint": "bone" } ])");
    const AnimDiagnostic* ambiguous = AnimRigFixture::FindCode(rig, "anim.mask.joint_ambiguous");
    ASSERT_NE(ambiguous, nullptr) << AnimRigFixture::Describe(rig);
    EXPECT_EQ(ambiguous->FieldPath, "$.data.layers[1].mask[0].joint");
}

TEST(AnimLayerMask, TheFirstLayerIsUnmaskedAndAMaskNeedsASkeleton)
{
    {
        AnimRigFixture fx;
        (void)fx.Skeletons.Register(kSkeleton, Biped());
        const AnimBoundRig& rig = BindMasked(fx, R"([ { "joint": "spine" } ])", R"([ { "joint": "root" } ])");
        const AnimDiagnostic* first = AnimRigFixture::FindCode(rig, "anim.mask.first_layer");
        ASSERT_NE(first, nullptr) << AnimRigFixture::Describe(rig);
        EXPECT_EQ(first->FieldPath, "$.data.layers[0].mask");
    }
    {
        AnimRigFixture fx;
        const AnimBoundRig& rig = BindMasked(fx, R"([ { "joint": "spine" } ])", {}, {});
        EXPECT_NE(AnimRigFixture::FindCode(rig, "anim.mask.no_skeleton"), nullptr) << AnimRigFixture::Describe(rig);
    }
    {
        // Named but not loaded.
        AnimRigFixture fx;
        const AnimBoundRig& rig = BindMasked(fx, R"([ { "joint": "spine" } ])");
        const AnimDiagnostic* missing = AnimRigFixture::FindCode(rig, "anim.rig.skeleton_unavailable");
        ASSERT_NE(missing, nullptr) << AnimRigFixture::Describe(rig);
        EXPECT_EQ(missing->Severity, AnimDiagnosticSeverity::Error);
    }
}

// A clip's joint indices are into its own skeleton; played by a rig posing
// another, they would move the wrong bones.
TEST(AnimLayerMask, EveryClipIsKeyedToTheRigsSkeleton)
{
    AnimRigFixture fx;
    (void)fx.Skeletons.Register(kSkeleton, Biped());
    (void)fx.Skeletons.Register("asset://anim/other.sskel", Biped());
    (void)fx.Tags().RegisterTag("Anim.Idle");
    fx.Clip("asset://anim/other_idle.sanim", 1.0f, {}, "asset://anim/other.sskel");
    (void)fx.Load("asset://anim/k.behaviors.sdata", kAnimBehaviorSetType,
                  R"({ "behaviors": [ { "tag": "Anim.Idle", "kind": "cyclic" } ] })");
    (void)fx.Load("asset://anim/k.slots.sdata", kAnimSlotMapType,
                  R"({ "rows": [ { "behavior": "Anim.Idle", "clip": "asset://anim/other_idle.sanim" } ] })");
    const AnimBoundRig& rig = fx.Bound(fx.Load("asset://anim/k.rig.sdata", kAnimRigType, R"({
        "skeleton": "asset://anim/biped.sskel",
        "behaviors": [ "asset://anim/k.behaviors.sdata" ], "slot_maps": [ "asset://anim/k.slots.sdata" ],
        "layers": [ { "name": "anim.layer.base", "idle": "Anim.Idle" } ] })"));
    const AnimDiagnostic* mismatch = AnimRigFixture::FindCode(rig, "anim.clip.skeleton_mismatch");
    ASSERT_NE(mismatch, nullptr) << AnimRigFixture::Describe(rig);
    EXPECT_EQ(mismatch->AssetPath, "asset://anim/other_idle.sanim");
    EXPECT_FALSE(rig.Valid);
}

// A binding is rebuilt when the skeleton registered under the rig's path
// changes, and only then; bound with no skeleton cache it has none to track.
TEST(AnimLayerMask, TheBindingFollowsTheSkeletonRegisteredUnderItsPath)
{
    AnimRigFixture fx;
    const AnimBoundRig& unloaded = BindMasked(fx, R"([ { "joint": "spine" } ])");
    EXPECT_FALSE(unloaded.Valid);
    const DataAssetHandle rig = fx.Data.Find("asset://anim/m.rig.sdata");

    AnimRigBindings bare(&fx.Data, &fx.Clips);
    (void)bare.Resolve(rig, fx.Entities);
    (void)bare.Resolve(rig, fx.Entities);
    EXPECT_EQ(bare.RebuildCount(), 1u);

    const std::uint64_t before = fx.Bindings().RebuildCount();
    (void)fx.Bound(rig);
    EXPECT_EQ(fx.Bindings().RebuildCount(), before);
    (void)fx.Skeletons.Register(kSkeleton, Biped());
    const AnimBoundRig& loaded = fx.Bound(rig);
    EXPECT_EQ(fx.Bindings().RebuildCount(), before + 1);
    EXPECT_TRUE(loaded.Valid) << AnimRigFixture::Describe(loaded);
}
