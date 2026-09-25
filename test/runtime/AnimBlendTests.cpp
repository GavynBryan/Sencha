// Blending: which policy absorbs a change -- the destination's own, or a
// pairwise override that is rare by construction -- and how the pose absorbs
// it.

#include "AnimRigFixture.h"

#include <gtest/gtest.h>

#include <format>
#include <string>

namespace
{
    // The character rig with blend override assets appended to it.
    struct BlendFixture : AnimRigFixture
    {
        DataAssetHandle Rig;
        EntityId Entity;

        BlendFixture()
        {
            AnimCharacterRig::RegisterTags(*this);
            Rig = AnimCharacterRig::Load(*this);
            Entity = Character(Rig);
        }

        void WithOverrides(std::initializer_list<std::string_view> paths)
        {
            std::string list;
            for (std::string_view path : paths)
                list += std::format("{}\"{}\"", list.empty() ? "" : ", ", path);
            Reload("asset://anim/character.rig.sdata", kAnimRigType, std::format(R"({{
                "facts": "asset://anim/character.facts.sdata", "requests": "asset://anim/character.requests.sdata",
                "behaviors": [ "asset://anim/character.behaviors.sdata" ], "slot_maps": [ "asset://anim/character.slots.sdata" ],
                "blend_overrides": [ {} ],
                "layers": [ {{ "name": "anim.layer.base", "selector": "asset://anim/character.selector.sdata",
                              "idle": "Anim.Locomotion.Idle" }} ] }})",
                                                                            list));
        }

        const AnimBoundRig& Bound() { return AnimRigFixture::Bound(Rig); }
    };

    constexpr std::string_view kSprintSnaps = R"({ "overrides": [
        { "from": "Anim.Locomotion.Walk", "to": "Anim.Locomotion.Sprint", "blend": { "in": "snap" } } ] })";
}

TEST(AnimBlendOverrides, AnOverrideReplacesTheDestinationsPolicyForItsPairOnly)
{
    BlendFixture fx;
    (void)fx.Load("asset://anim/a.blend.sdata", kAnimBlendOverridesType, kSprintSnaps);
    fx.WithOverrides({ "asset://anim/a.blend.sdata" });
    const AnimBoundRig& rig = fx.Bound();
    ASSERT_TRUE(rig.Valid) << AnimRigFixture::Describe(rig);

    const AnimBoundBlendOverride* overridden = nullptr;
    const AnimBlendPolicy walkToSprint =
        rig.ResolveBlend(fx.Tag("Anim.Locomotion.Walk"), fx.Tag("Anim.Locomotion.Sprint"), &overridden);
    EXPECT_EQ(walkToSprint.In, AnimBlendMode::Snap);
    ASSERT_NE(overridden, nullptr);
    EXPECT_EQ(overridden->DeclaredIn, "asset://anim/a.blend.sdata");

    // Any other way into sprint blends by sprint's own policy.
    const AnimBlendPolicy idleToSprint =
        rig.ResolveBlend(fx.Tag("Anim.Locomotion.Idle"), fx.Tag("Anim.Locomotion.Sprint"), &overridden);
    EXPECT_EQ(idleToSprint.In, AnimBlendMode::Inertialize);
    EXPECT_FLOAT_EQ(idleToSprint.InMs, 150.0f);
    EXPECT_EQ(overridden, nullptr);
}

TEST(AnimBlendOverrides, ALaterAssetReplacesAnEarlierOnesPair)
{
    BlendFixture fx;
    (void)fx.Load("asset://anim/a.blend.sdata", kAnimBlendOverridesType, kSprintSnaps);
    (void)fx.Load("asset://anim/b.blend.sdata", kAnimBlendOverridesType, R"({ "overrides": [
        { "from": "Anim.Locomotion.Walk", "to": "Anim.Locomotion.Sprint",
          "blend": { "in": "crossfade", "in_ms": 80 } } ] })");
    fx.WithOverrides({ "asset://anim/a.blend.sdata", "asset://anim/b.blend.sdata" });
    const AnimBoundRig& rig = fx.Bound();
    ASSERT_EQ(rig.BlendOverrides.size(), 1u);
    const AnimBlendPolicy policy = rig.ResolveBlend(fx.Tag("Anim.Locomotion.Walk"), fx.Tag("Anim.Locomotion.Sprint"));
    EXPECT_EQ(policy.In, AnimBlendMode::Crossfade);
    EXPECT_FLOAT_EQ(policy.InMs, 80.0f);
}

// Overrides are rare by construction: past half the cap warns, past the cap
// fails to bind, and raising the cap rebinds.
TEST(AnimBlendOverrides, TheCapWarnsAtHalfAndFailsPastIt)
{
    BlendFixture fx;
    fx.Entities.GetResource<AnimRigLimits>().BlendOverrideCap = 2;
    (void)fx.Load("asset://anim/c.blend.sdata", kAnimBlendOverridesType, R"({ "overrides": [
        { "from": "Anim.Locomotion.Walk", "to": "Anim.Locomotion.Sprint", "blend": { "in": "snap" } },
        { "from": "Anim.Locomotion.Sprint", "to": "Anim.Locomotion.Walk", "blend": { "in": "snap" } },
        { "from": "Anim.Locomotion.Idle", "to": "Anim.Locomotion.Walk", "blend": { "in": "snap" } } ] })");
    fx.WithOverrides({ "asset://anim/c.blend.sdata" });
    const AnimDiagnostic* cap = AnimRigFixture::FindCode(fx.Bound(), "anim.blend.override_cap");
    ASSERT_NE(cap, nullptr) << AnimRigFixture::Describe(fx.Bound());
    EXPECT_EQ(cap->FieldPath, "$.data.blend_overrides");
    EXPECT_FALSE(fx.Bound().Valid);

    const std::uint64_t before = fx.Bindings().RebuildCount();
    fx.Entities.GetResource<AnimRigLimits>().BlendOverrideCap = 4;
    const AnimBoundRig& raised = fx.Bound();
    EXPECT_EQ(fx.Bindings().RebuildCount(), before + 1);
    EXPECT_TRUE(raised.Valid) << AnimRigFixture::Describe(raised);
    const AnimDiagnostic* count = AnimRigFixture::FindCode(raised, "anim.blend.override_count");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->Severity, AnimDiagnosticSeverity::Warning);
}

TEST(AnimBlendOverrides, AnOverrideNamesTwoDeclaredBehaviorsOnce)
{
    AnimRigFixture fx;
    const auto error = [&](std::string_view json) { return fx.CompileError(kAnimBlendOverridesType, json); };
    EXPECT_NE(error(R"({ "overrides": [ { "from": "Anim.A", "to": "Anim.A" } ] })").find("to itself"),
              std::string::npos);
    EXPECT_NE(error(R"({ "overrides": [ { "from": "Anim.A", "to": "Anim.B" }, { "from": "Anim.A", "to": "Anim.B" } ] })")
                  .find("overridden twice"),
              std::string::npos);
    EXPECT_NE(error(R"({ "overrides": [ { "from": "Anim.A", "to": "Anim.B", "blend": { "in_ms": -5 } } ] })")
                  .find("$.data.overrides[0].blend"),
              std::string::npos);

    BlendFixture bound;
    (void)bound.Tags().RegisterTag("Anim.Undeclared");
    (void)bound.Load("asset://anim/u.blend.sdata", kAnimBlendOverridesType, R"({ "overrides": [
        { "from": "Anim.Locomotion.Walk", "to": "Anim.Undeclared" } ] })");
    bound.WithOverrides({ "asset://anim/u.blend.sdata" });
    const AnimDiagnostic* undeclared = AnimRigFixture::FindCode(bound.Bound(), "anim.blend.undeclared_behavior");
    ASSERT_NE(undeclared, nullptr) << AnimRigFixture::Describe(bound.Bound());
    EXPECT_EQ(undeclared->AssetPath, "asset://anim/u.blend.sdata");
    EXPECT_EQ(undeclared->FieldPath, "$.data.overrides[0].to");
}

// Phase is part of the policy, so an override can carry phase for its pair.
TEST(AnimBlendOverrides, AnOverrideCanCarryPhaseForItsPair)
{
    BlendFixture fx;
    (void)fx.Tags().RegisterTag("Anim.Sync.Locomotion");
    fx.Reload("asset://anim/character.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Locomotion.Idle", "kind": "cyclic" },
        { "tag": "Anim.Locomotion.Walk", "kind": "cyclic", "sync_group": "Anim.Sync.Locomotion" },
        { "tag": "Anim.Locomotion.Sprint", "kind": "cyclic", "sync_group": "Anim.Sync.Locomotion" },
        { "tag": "Anim.Action.Land", "kind": "one_shot" },
        { "tag": "Anim.Action.Reload", "kind": "one_shot",
          "latch": { "mode": "until_request_ends", "interruptible_by": "tags", "tags": [ "Anim.Death" ] } },
        { "tag": "Anim.Death", "kind": "hold" } ] })");
    (void)fx.Load("asset://anim/p.blend.sdata", kAnimBlendOverridesType, R"({ "overrides": [
        { "from": "Anim.Locomotion.Walk", "to": "Anim.Locomotion.Sprint", "blend": { "phase": "carry" } } ] })");
    fx.WithOverrides({ "asset://anim/p.blend.sdata" });
    ASSERT_TRUE(fx.Bound().Valid) << AnimRigFixture::Describe(fx.Bound());

    fx.Motion(fx.Entity).Speed = 1.0f;
    fx.Tick(30);
    fx.Motion(fx.Entity).Speed = 3.0f;
    fx.Tick();
    EXPECT_NEAR(fx.Playing(fx.Entity).TimeSeconds, 0.4f, 1e-4f);
}
