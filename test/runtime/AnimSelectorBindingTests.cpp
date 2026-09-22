// Selectors flatten when a rig binds: delegation and extension points become
// rules of the layer's selector, nested conditions concatenate, and the
// pairings the architecture forbids are located errors rather than surprises
// at runtime.

#include "AnimRigFixture.h"

#include <algorithm>
#include <format>

namespace
{
    constexpr const char* kTags[] = { "Anim.Idle", "Anim.Fire", "Anim.Fire.Pistol", "Anim.Fire.Shotgun",
                                      "Anim.Reload", "Anim.Climb", "anim.intent.fire", "anim.intent.reload" };

    struct Selectors : AnimRigFixture
    {
        Selectors()
        {
            for (const char* tag : kTags)
                (void)Tags().RegisterTag(tag);
            Clip("asset://anim/clip.sanim", 1.0f);
            (void)Load("asset://anim/s.facts.sdata", kAnimFactSchemaType, R"({
                "slots": [ { "name": "Armed", "kind": "bool" }, { "name": "Shotgun", "kind": "bool" } ] })");
            (void)Load("asset://anim/s.requests.sdata", kAnimRequestSchemaType, R"({
                "intents": [ { "intent": "anim.intent.fire", "params": [] },
                             { "intent": "anim.intent.reload", "params": [] } ] })");
            (void)Load("asset://anim/s.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
                { "tag": "Anim.Idle", "kind": "cyclic" },
                { "tag": "Anim.Fire.Pistol", "kind": "one_shot" },
                { "tag": "Anim.Fire.Shotgun", "kind": "one_shot" },
                { "tag": "Anim.Reload", "kind": "one_shot", "late_join": "reconstruct",
                  "latch": { "mode": "until_request_ends" } },
                { "tag": "Anim.Climb", "kind": "cyclic", "root_motion": true } ] })");
        }

        // A rig whose one layer uses `selector`, with `extra` rig fields.
        DataAssetHandle Rig(std::string_view selector, std::string_view extra = {})
        {
            (void)Load("asset://anim/s.selector.sdata", kAnimSelectorType, selector);
            return Load("asset://anim/s.rig.sdata", kAnimRigType,
                        std::string(R"({ "facts": "asset://anim/s.facts.sdata", "requests": "asset://anim/s.requests.sdata",
                            "behaviors": [ "asset://anim/s.behaviors.sdata" ], )")
                            + std::string(extra)
                            + R"( "layers": [ { "name": "anim.layer.base", "selector": "asset://anim/s.selector.sdata" } ] })");
        }
    };

    constexpr std::string_view kWeapon = R"({ "rules": [
        { "name": "shotgun", "priority": 5, "enter": [ { "fact": "Shotgun" } ], "behavior": "Anim.Fire.Shotgun" },
        { "name": "pistol", "priority": 0, "enter": [], "behavior": "Anim.Fire.Pistol" } ] })";
}

TEST(AnimSelectorBinding, DelegationFlattensUnderTheDelegatingRule)
{
    Selectors fx;
    (void)fx.Load("asset://anim/weapon.selector.sdata", kAnimSelectorType, kWeapon);
    const DataAssetHandle rig = fx.Rig(R"({ "rules": [
        { "name": "firing", "priority": 20, "enter": [ { "fact": "Armed" }, { "request": "anim.intent.fire" } ],
          "delegate": "asset://anim/weapon.selector.sdata" },
        { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Idle" } ] })");
    const AnimBoundRig& bound = fx.Bound(rig);
    ASSERT_TRUE(bound.Valid) << AnimRigFixture::Describe(bound);

    const std::vector<AnimBoundRule>& rules = bound.Selectors[0].Rules;
    ASSERT_EQ(rules.size(), 3u);
    EXPECT_EQ(rules[0].Label, "shotgun");
    EXPECT_EQ(rules[1].Label, "pistol");
    EXPECT_EQ(rules[2].Label, "idle");
    // The parent's two rows, then the child's.
    EXPECT_EQ(rules[0].Enter.RowEnds.size(), 3u);
    EXPECT_EQ(rules[1].Enter.RowEnds.size(), 2u);
    // A delegated rule stands where its parent stood.
    EXPECT_EQ(rules[0].Band, 20);
    EXPECT_EQ(rules[1].Band, 20);
    ASSERT_EQ(rules[0].Source.size(), 2u);
    EXPECT_EQ(rules[0].Source[0].Name, "firing");
    EXPECT_EQ(rules[0].Source[1].Selector, "asset://anim/weapon.selector.sdata");
    EXPECT_NE(rules[0].Key, rules[1].Key);
    // Every flattened row maps back to where it was written.
    ASSERT_EQ(rules[0].EnterRows.size(), 3u);
    EXPECT_EQ(rules[0].EnterRows[0].Selector, "asset://anim/s.selector.sdata");
    EXPECT_EQ(rules[0].EnterRows[1].Row, 1u);
    EXPECT_EQ(rules[0].EnterRows[2].Selector, "asset://anim/weapon.selector.sdata");
    EXPECT_EQ(rules[0].EnterRows[2].Rule, 0u);
}

TEST(AnimSelectorBinding, AnExtensionPointContributesOnlyOnceBound)
{
    Selectors fx;
    (void)fx.Load("asset://anim/weapon.selector.sdata", kAnimSelectorType, kWeapon);
    const std::string_view selector = R"({ "rules": [
        { "name": "weapon", "priority": 20, "enter": [ { "fact": "Armed" } ], "extension": "Weapon" },
        { "name": "idle", "priority": 0, "enter": [], "behavior": "Anim.Idle" } ] })";

    const DataAssetHandle bare = fx.Rig(selector);
    ASSERT_TRUE(fx.Bound(bare).Valid) << AnimRigFixture::Describe(fx.Bound(bare));
    EXPECT_EQ(fx.Bound(bare).Selectors[0].Rules.size(), 1u);

    fx.Reload("asset://anim/s.rig.sdata", kAnimRigType, R"({ "facts": "asset://anim/s.facts.sdata",
        "requests": "asset://anim/s.requests.sdata", "behaviors": [ "asset://anim/s.behaviors.sdata" ],
        "extensions": [ { "name": "Weapon", "selector": "asset://anim/weapon.selector.sdata" } ],
        "layers": [ { "name": "anim.layer.base", "selector": "asset://anim/s.selector.sdata" } ] })");
    EXPECT_EQ(fx.Bound(bare).Selectors[0].Rules.size(), 3u);
}

TEST(AnimSelectorBinding, DelegationDeeperThanFourIsAnError)
{
    Selectors fx;
    for (int i = 1; i <= 4; ++i)
        (void)fx.Load(std::format("asset://anim/d{}.selector.sdata", i), kAnimSelectorType,
                      i < 4 ? std::format(R"({{ "rules": [ {{ "enter": [], "delegate": "asset://anim/d{}.selector.sdata" }} ] }})", i + 1)
                            : std::string(R"({ "rules": [ { "enter": [], "behavior": "Anim.Idle" } ] })"));
    const DataAssetHandle rig =
        fx.Rig(R"({ "rules": [ { "enter": [], "delegate": "asset://anim/d1.selector.sdata" } ] })");
    const AnimDiagnostic* depth = AnimRigFixture::FindCode(fx.Bound(rig), "anim.selector.depth");
    ASSERT_NE(depth, nullptr) << AnimRigFixture::Describe(fx.Bound(rig));
    EXPECT_EQ(depth->AssetPath, "asset://anim/d3.selector.sdata");
    EXPECT_EQ(depth->FieldPath, "$.data.rules[0].delegate");
}

TEST(AnimSelectorBinding, TheArchitecturesPairingsAreEnforced)
{
    Selectors fx;
    const DataAssetHandle rig = fx.Rig(R"({ "rules": [
        { "name": "unknown", "priority": 50, "enter": [], "behavior": "Anim.Fire" },
        { "name": "reload by fact", "priority": 40, "enter": [ { "fact": "Armed" } ], "behavior": "Anim.Reload" },
        { "name": "climb by fact", "priority": 30, "enter": [ { "fact": "Armed" } ], "behavior": "Anim.Climb" },
        { "name": "reload by two", "priority": 20,
          "enter": [ { "request": "anim.intent.reload" }, { "request": "anim.intent.fire" } ],
          "behavior": "Anim.Reload" } ] })");
    const AnimBoundRig& bound = fx.Bound(rig);
    EXPECT_FALSE(bound.Valid);
    const auto at = [&](std::string_view code) {
        const AnimDiagnostic* found = AnimRigFixture::FindCode(bound, code);
        return found != nullptr ? found->FieldPath : std::string("(missing)");
    };
    // Tags are declared; behaviors are what behavior sets declare.
    EXPECT_EQ(at("anim.selector.undeclared_behavior"), "$.data.rules[0].behavior");
    // Reachable without a request: no start tick to rebuild from, nothing to
    // anchor motion to.
    EXPECT_EQ(at("anim.selector.cosmetic_reconstruct"), "$.data.rules[1].enter");
    EXPECT_EQ(at("anim.selector.cosmetic_root_motion"), "$.data.rules[2].enter");
    // A request latch follows exactly one request.
    EXPECT_EQ(at("anim.selector.latch_request"), "$.data.rules[1].enter");
    const auto latchErrors = std::count_if(bound.Diagnostics.begin(), bound.Diagnostics.end(),
                                           [](const AnimDiagnostic& d) { return d.Code == "anim.selector.latch_request"; });
    EXPECT_EQ(latchErrors, 2);
}

TEST(AnimSelectorBinding, CooldownsAreBoundedPerLayer)
{
    Selectors fx;
    std::string rules = R"({ "rules": [ )";
    for (int i = 0; i < 5; ++i)
        rules += std::format(R"({}{{ "priority": {}, "enter": [], "behavior": "Anim.Idle", "cooldown_ms": 100 }})",
                             i ? ", " : "", i);
    rules += " ] }";
    const DataAssetHandle rig = fx.Rig(rules);
    EXPECT_NE(AnimRigFixture::FindCode(fx.Bound(rig), "anim.selector.cooldowns"), nullptr);
}

TEST(AnimSelectorBinding, BehaviorSetsOverrideByTagInOrder)
{
    Selectors fx;
    (void)fx.Load("asset://anim/mod.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Idle", "kind": "hold" } ] })");
    const DataAssetHandle rig = fx.Load("asset://anim/o.rig.sdata", kAnimRigType, R"({
        "behaviors": [ "asset://anim/s.behaviors.sdata", "asset://anim/mod.behaviors.sdata" ],
        "layers": [ { "name": "anim.layer.base" } ] })");
    const AnimBoundBehavior* idle = fx.Bound(rig).FindBehavior(fx.Tag("Anim.Idle"));
    ASSERT_NE(idle, nullptr);
    EXPECT_EQ(idle->Policy.Kind, AnimBehaviorKind::Hold);
    EXPECT_EQ(idle->DeclaredIn, "asset://anim/mod.behaviors.sdata");
}

TEST(AnimSelectorBinding, BehaviorPoliciesRejectImpossibleCombinations)
{
    Selectors fx;
    EXPECT_NE(fx.CompileError(kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Idle", "kind": "one_shot", "latch": { "mode": "until_complete", "on_interrupt": "cancel_section" } } ] })"),
              "");
    EXPECT_NE(fx.CompileError(kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Idle", "kind": "cyclic", "latch": { "mode": "until_complete" } } ] })"),
              "");
    EXPECT_NE(fx.CompileError(kAnimSelectorType, R"({ "rules": [
        { "enter": [], "behavior": "Anim.Idle", "delegate": "asset://anim/x.selector.sdata" } ] })"),
              "");
}
