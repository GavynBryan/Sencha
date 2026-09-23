// Layers: each composes into the pose at a weight its selector's weight rules
// choose from this tick's facts and requests, or at the rig's constant.

#include "AnimRigFixture.h"

#include <anim/AnimFacts.h>

#include <gtest/gtest.h>

#include <string>

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

    struct Layered : AnimRigFixture
    {
        DataAssetHandle Rig;
        EntityId Entity;

        explicit Layered(std::string_view upperSelector = kUpperSelector)
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
    Layered fx;
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
    Layered fx(R"({ "rules": [
        { "name": "none", "priority": 0, "enter": [], "behavior": "Anim.Upper.None" },
        { "name": "aim", "priority": 10, "weight_fact": "Crouched", "enter": [] } ] })");
    const AnimDiagnostic* fact = AnimRigFixture::FindCode(fx.Bound(fx.Rig), "anim.selector.weight_fact");
    ASSERT_NE(fact, nullptr) << AnimRigFixture::Describe(fx.Bound(fx.Rig));
    EXPECT_EQ(fact->AssetPath, "asset://anim/l.upper.sdata");
    EXPECT_EQ(fact->FieldPath, "$.data.rules[1].weight_fact");
}
