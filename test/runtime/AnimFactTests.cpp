// Fact schemas bind per World into one layout, derivations run as a closed
// program over it, and gathering fills it from gameplay.

#include <gtest/gtest.h>

#include <anim/AnimFactEvaluation.h>
#include <anim/AnimFactGatherSystem.h>
#include <anim/AnimRigCompositionSystem.h>
#include <anim/AnimFactProviders.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimationRegistration.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/json/JsonParser.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/MovementAnimFacts.h>
#include <movement/components/CharacterFacts.h>
#include <world/ComponentRegistrar.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace
{
    constexpr double kTick = 1.0 / 60.0;

    constexpr std::string_view kCore = R"({
        "slots": [
            { "name": "Grounded", "kind": "bool" },
            { "name": "Speed", "kind": "float" },
            { "name": "VerticalSpeed", "kind": "float" },
            { "name": "Dead", "kind": "bool" }
        ]
    })";

    struct AnimFixture
    {
        DataAssetTypeRegistry Types;
        DataSchemaRegistry Schemas;
        DataAssetCache Data;
        World Entities;

        AnimFixture()
        {
            RegisterAnimFactSchema(Types, Schemas);
            RegisterAnimRequestSchema(Types, Schemas);
            RegisterAnimRigData(Types, Schemas);

            Entities.AddResource<GameplayTagRegistry>();
            ComponentRegistrar registrar(Entities);
            RegisterAnimationComponents(registrar);
            Entities.RegisterComponent<SupportState>();
            Entities.RegisterComponent<KinematicState>();
            InstallAnimationVocabulary(Entities);
            Entities.SetResource(AnimRigBindings{ &Data, nullptr });

            (void)Load("asset://animation/engine.facts.sdata", kAnimFactSchemaType, kCore);
        }

        GameplayTagRegistry& Tags() { return Entities.GetResource<GameplayTagRegistry>(); }
        AnimRigBindings& Bindings() { return Entities.GetResource<AnimRigBindings>(); }

        DataAssetHandle Load(std::string_view path, std::string_view type, std::string_view json)
        {
            const DataAssetCompileResult compiled = Types.Find(type)->Compile(*JsonParse(json));
            EXPECT_TRUE(compiled.IsValid()) << path << ": " << compiled.Error;
            return Data.Register(path, std::string(type), compiled.Value);
        }

        void Reload(std::string_view path, std::string_view type, std::string_view json)
        {
            const DataAssetCompileResult compiled = Types.Find(type)->Compile(*JsonParse(json));
            ASSERT_TRUE(compiled.IsValid()) << compiled.Error;
            ASSERT_TRUE(Data.ReloadInPlace(path, type, compiled.Value));
        }

        // A schema over the core one and a rig naming it.
        DataAssetHandle Rig(std::string_view schemaData,
                            std::string_view rigExtra = {},
                            std::string_view name = "game")
        {
            const std::string schemaPath = std::format("asset://animation/{}.facts.sdata", name);
            (void)Load(schemaPath, kAnimFactSchemaType, schemaData);
            const std::string rig = std::format(
                R"({{ "facts": "{}", {} "layers": [ {{ "name": "anim.layer.base" }} ] }})",
                schemaPath, rigExtra);
            return Load(std::format("asset://animation/{}.rig.sdata", name), kAnimRigType, rig);
        }

        // A rig over the core schema with these derived facts.
        DataAssetHandle DerivedRig(std::string_view derived)
        {
            return Rig(std::format(
                R"({{ "extends": "asset://animation/engine.facts.sdata", "derived": {} }})", derived));
        }

        const AnimBoundRig& Bound(DataAssetHandle rig)
        {
            const AnimBoundRig* bound = Bindings().Resolve(rig, Entities);
            EXPECT_NE(bound, nullptr);
            return *bound;
        }
    };

    SupportState Stable()
    {
        SupportState support;
        support.Kind = SupportKind::Stable;
        return support;
    }

    std::string Describe(const AnimBoundRig& rig)
    {
        std::string text;
        for (const AnimDiagnostic& diagnostic : rig.Diagnostics)
            text += FormatAnimDiagnostic(diagnostic) + "\n";
        return text;
    }

    const AnimDiagnostic* FindCode(const AnimBoundRig& rig, std::string_view code)
    {
        for (const AnimDiagnostic& diagnostic : rig.Diagnostics)
        {
            if (diagnostic.Code == code)
                return &diagnostic;
        }
        return nullptr;
    }

    // Drives one entity's slots tick by tick without a World.
    struct Driver
    {
        explicit Driver(const AnimBoundRig& rig) : Rig(rig) {}

        const AnimBoundRig& Rig;
        std::vector<std::uint32_t> Values = std::vector<std::uint32_t>(kAnimFactsLarge, 0u);
        AnimFactHistory History;

        void Set(std::string_view slot, bool value)
        {
            Values[static_cast<std::size_t>(Rig.FindSlot(slot))] = AnimFactFromBool(value);
        }
        void Set(std::string_view slot, float value)
        {
            Values[static_cast<std::size_t>(Rig.FindSlot(slot))] = AnimFactFromFloat(value);
        }
        void Step(AnimTick tick) { EvaluateAnimDerivations(Rig, Values, History, tick, kTick); }
        [[nodiscard]] bool Bool(std::string_view slot) const
        {
            return AnimFactToBool(Values[static_cast<std::size_t>(Rig.FindSlot(slot))]);
        }
        [[nodiscard]] float Float(std::string_view slot) const
        {
            return AnimFactToFloat(Values[static_cast<std::size_t>(Rig.FindSlot(slot))]);
        }
    };
}

// ─── Binding ────────────────────────────────────────────────────────────────

TEST(AnimRigBinding, MergesTheChainBaseFirstAndResolvesNames)
{
    AnimFixture fx;
    ASSERT_TRUE(fx.Tags().RegisterTag("anim.intent.reload").has_value());
    (void)fx.Load("asset://animation/requests.sdata", kAnimRequestSchemaType, R"({
        "intents": [ { "intent": "anim.intent.reload",
                       "params": [ { "name": "rate", "kind": "float" } ] } ] })");
    (void)fx.Load("asset://animation/game.facts.sdata", kAnimFactSchemaType, R"({
        "extends": "asset://animation/engine.facts.sdata",
        "slots": [ { "name": "Crouched", "kind": "bool" },
                   { "name": "CameraYaw", "kind": "float", "local": true } ],
        "derived": [
            { "name": "JustLanded", "op": "edge", "source": { "fact": "Grounded" },
              "direction": "rising", "window_ms": 120 },
            { "name": "LookingAway", "op": "compare", "source": { "fact": "CameraYaw" },
              "compare": "gt", "constant": 1.5 } ] })");
    const DataAssetHandle rig = fx.Load("asset://animation/game.rig.sdata", kAnimRigType, R"({
        "facts": "asset://animation/game.facts.sdata",
        "requests": "asset://animation/requests.sdata",
        "layers": [ { "name": "anim.layer.base" },
                    { "name": "anim.layer.upper", "mode": "additive", "weight": 0.5 } ] })");

    const AnimBoundRig& bound = fx.Bound(rig);
    ASSERT_TRUE(bound.Valid) << Describe(bound);

    // Base slots take the first indices, then the extension's, then every
    // derived fact after every declared slot.
    const std::vector<std::string> expected{ "Grounded", "Speed", "VerticalSpeed", "Dead",
                                             "Crouched", "CameraYaw", "JustLanded", "LookingAway" };
    ASSERT_EQ(bound.Slots.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
        EXPECT_EQ(bound.Slots[i].Name, expected[i]) << i;
    EXPECT_EQ(bound.Slots[0].DeclaredIn, "asset://animation/engine.facts.sdata");
    EXPECT_EQ(bound.Slots[4].DeclaredIn, "asset://animation/game.facts.sdata");

    // A fact derived from a local fact is local.
    EXPECT_FALSE(bound.Slots[6].Local);
    EXPECT_TRUE(bound.Slots[7].Local);
    EXPECT_EQ(bound.Slots[6].Derivation, 0);
    EXPECT_EQ(bound.Derivations[0].Sources[0].Slot, 0);
    EXPECT_EQ(bound.Derivations[0].Result, 6);
    EXPECT_TRUE(bound.HasTemporalDerivations);
    EXPECT_FLOAT_EQ(bound.HorizonMs, 120.0f);

    ASSERT_EQ(bound.Layers.size(), 2u);
    EXPECT_EQ(bound.Layers[0].Name, fx.Tags().FindTag("anim.layer.base"));
    EXPECT_EQ(bound.Layers[1].Mode, AnimLayerMode::Additive);
    EXPECT_FLOAT_EQ(bound.Layers[1].Weight, 0.5f);

    ASSERT_TRUE(bound.HasRequestSchema);
    const AnimBoundIntent* reload = bound.FindIntent(fx.Tags().FindTag("anim.intent.reload"));
    ASSERT_NE(reload, nullptr);
    ASSERT_EQ(reload->Params.size(), 1u);
    EXPECT_EQ(reload->Params[0].Name, "rate");
}

TEST(AnimRigBinding, ASlotRedeclaredAnywhereInTheChainIsALocatedError)
{
    AnimFixture fx;
    const DataAssetHandle rig = fx.Rig(R"({
        "extends": "asset://animation/engine.facts.sdata",
        "slots": [ { "name": "Speed", "kind": "int" } ] })");

    const AnimBoundRig& bound = fx.Bound(rig);
    EXPECT_FALSE(bound.Valid);
    const AnimDiagnostic* diagnostic = FindCode(bound, "anim.fact.duplicate_slot");
    ASSERT_NE(diagnostic, nullptr) << Describe(bound);
    EXPECT_EQ(diagnostic->AssetPath, "asset://animation/game.facts.sdata");
    EXPECT_EQ(diagnostic->FieldPath, "$.data.slots[0].name");
}

TEST(AnimRigBinding, ADerivationMayReadOnlyEarlierFacts)
{
    AnimFixture fx;
    const DataAssetHandle rig = fx.DerivedRig(R"([
        { "name": "A", "op": "not", "source": { "fact": "B" } },
        { "name": "B", "op": "not", "source": { "fact": "Grounded" } } ])");

    const AnimBoundRig& bound = fx.Bound(rig);
    EXPECT_FALSE(bound.Valid);
    const AnimDiagnostic* diagnostic = FindCode(bound, "anim.fact.unknown_operand");
    ASSERT_NE(diagnostic, nullptr) << Describe(bound);
    EXPECT_EQ(diagnostic->FieldPath, "$.data.derived[0].source.fact");
}

TEST(AnimRigBinding, OperandKindsAreCheckedPerOp)
{
    AnimFixture fx;
    const DataAssetHandle rig = fx.DerivedRig(R"([
        { "name": "Moving", "op": "edge", "source": { "fact": "Speed" },
          "direction": "rising", "window_ms": 100 } ])");

    const AnimBoundRig& bound = fx.Bound(rig);
    EXPECT_FALSE(bound.Valid);
    EXPECT_NE(FindCode(bound, "anim.fact.operand_kind"), nullptr) << Describe(bound);
}

TEST(AnimRigBinding, UnknownTagsAreReportedAndNeverRegistered)
{
    AnimFixture fx;
    (void)fx.Load("asset://animation/requests.sdata", kAnimRequestSchemaType, R"({
        "intents": [ { "intent": "anim.intent.misspelt", "params": [] } ] })");
    const DataAssetHandle rig = fx.Load("asset://animation/game.rig.sdata", kAnimRigType, R"({
        "requests": "asset://animation/requests.sdata",
        "layers": [ { "name": "anim.layer.nowhere" } ] })");
    const std::size_t tagsBefore = fx.Tags().Size();

    const AnimBoundRig& bound = fx.Bound(rig);
    EXPECT_FALSE(bound.Valid);
    EXPECT_NE(FindCode(bound, "anim.request.intent_unresolved"), nullptr) << Describe(bound);
    const AnimDiagnostic* layer = FindCode(bound, "anim.rig.layer_unresolved");
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(layer->FieldPath, "$.data.layers[0].name");
    EXPECT_EQ(fx.Tags().Size(), tagsBefore);
    EXPECT_FALSE(fx.Tags().FindTag("anim.layer.nowhere").IsValid());
}

TEST(AnimRigBinding, AMissingSchemaIsAnErrorNotAnEmptyLayout)
{
    AnimFixture fx;
    const DataAssetHandle rig = fx.Load("asset://animation/game.rig.sdata", kAnimRigType, R"({
        "facts": "asset://animation/absent.facts.sdata",
        "layers": [ { "name": "anim.layer.base" } ] })");

    const AnimBoundRig& bound = fx.Bound(rig);
    EXPECT_FALSE(bound.Valid);
    EXPECT_NE(FindCode(bound, "anim.asset.not_resident"), nullptr) << Describe(bound);
}

TEST(AnimRigBinding, RebindsWhenAnythingItReadChanges)
{
    AnimFixture fx;
    const DataAssetHandle rig = fx.DerivedRig(R"([
        { "name": "Falling", "op": "compare", "source": { "fact": "VerticalSpeed" },
          "compare": "lt", "constant": 0 } ])");

    const std::uint64_t first = fx.Bound(rig).Generation;
    EXPECT_EQ(fx.Bound(rig).Generation, first);
    EXPECT_EQ(fx.Bindings().RebuildCount(), 1u);

    // A schema in the chain reloading.
    fx.Reload("asset://animation/engine.facts.sdata", kAnimFactSchemaType, R"({
        "slots": [ { "name": "Grounded", "kind": "bool" },
                   { "name": "Speed", "kind": "float" },
                   { "name": "VerticalSpeed", "kind": "float" },
                   { "name": "Dead", "kind": "bool" },
                   { "name": "Stunned", "kind": "bool" } ] })");
    const AnimBoundRig& reloaded = fx.Bound(rig);
    EXPECT_NE(reloaded.Generation, first);
    EXPECT_GE(reloaded.FindSlot("Stunned"), 0);
    const std::uint64_t second = reloaded.Generation;

    // The vocabulary growing.
    ASSERT_TRUE(fx.Tags().RegisterTag("game.new_tag").has_value());
    const std::uint64_t third = fx.Bound(rig).Generation;
    EXPECT_NE(third, second);

    // A provider binding.
    AnimFactProviders& providers = fx.Entities.GetResource<AnimFactProviders>();
    ASSERT_TRUE(BindMovementAnimFacts(providers));
    const AnimBoundRig& provided = fx.Bound(rig);
    EXPECT_NE(provided.Generation, third);
    EXPECT_GE(provided.Slots[static_cast<std::size_t>(provided.FindSlot("Speed"))].Provider, 0);
    EXPECT_LT(provided.Slots[static_cast<std::size_t>(provided.FindSlot("Dead"))].Provider, 0);
}

TEST(AnimRigBinding, AProviderOfTheWrongKindIsLocatedAtTheSlot)
{
    AnimFixture fx;
    ASSERT_TRUE(BindMovementAnimFacts(fx.Entities.GetResource<AnimFactProviders>()));
    const DataAssetHandle rig = fx.Rig(R"({
        "slots": [ { "name": "Crouched", "kind": "bool" }, { "name": "Speed", "kind": "int" } ] })");

    const AnimBoundRig& bound = fx.Bound(rig);
    EXPECT_FALSE(bound.Valid);
    const AnimDiagnostic* diagnostic = FindCode(bound, "anim.fact.provider_kind");
    ASSERT_NE(diagnostic, nullptr) << Describe(bound);
    EXPECT_EQ(diagnostic->AssetPath, "asset://animation/game.facts.sdata");
    EXPECT_EQ(diagnostic->FieldPath, "$.data.slots[1].kind");
    EXPECT_LT(bound.Slots[1].Provider, 0);
}

TEST(AnimFactProviders, OneSlotHasOneSource)
{
    AnimFactProviders providers;
    ASSERT_TRUE(BindMovementAnimFacts(providers));
    const std::uint64_t revision = providers.Revision();
    EXPECT_FALSE(BindMovementAnimFacts(providers));
    EXPECT_TRUE(providers.Unbind("Speed"));
    EXPECT_LT(providers.IndexOf("Speed"), 0);
    EXPECT_NE(providers.Revision(), revision);
}

// ─── Derivations ────────────────────────────────────────────────────────────

TEST(AnimFactDerivation, EdgeHoldsForItsWindowAfterTheTransition)
{
    AnimFixture fx;
    const AnimBoundRig& rig = fx.Bound(fx.DerivedRig(R"([
        { "name": "JustLanded", "op": "edge", "source": { "fact": "Grounded" },
          "direction": "rising", "window_ms": 120 } ])"));
    ASSERT_TRUE(rig.Valid) << Describe(rig);
    Driver d(rig);

    // Grounded from the first observed tick is not a landing: there was no
    // prior value to rise from.
    d.Set("Grounded", true);
    d.Step(0);
    EXPECT_FALSE(d.Bool("JustLanded"));

    d.Set("Grounded", false);
    for (AnimTick t = 1; t < 10; ++t)
        d.Step(t);
    d.Set("Grounded", true);
    d.Step(10);
    EXPECT_TRUE(d.Bool("JustLanded"));
    d.Step(17); // 116.7 ms after
    EXPECT_TRUE(d.Bool("JustLanded"));
    d.Step(18); // 133.3 ms after
    EXPECT_FALSE(d.Bool("JustLanded"));
}

TEST(AnimFactDerivation, MinDurationOfANegatedOperand)
{
    AnimFixture fx;
    const AnimBoundRig& rig = fx.Bound(fx.DerivedRig(R"([
        { "name": "Airborne", "op": "min_duration",
          "source": { "fact": "Grounded", "not": true }, "window_ms": 80 } ])"));
    ASSERT_TRUE(rig.Valid) << Describe(rig);
    Driver d(rig);

    d.Set("Grounded", false);
    for (AnimTick t = 0; t <= 4; ++t)
    {
        d.Step(t);
        EXPECT_FALSE(d.Bool("Airborne")) << t; // 66.7 ms at tick 4
    }
    d.Step(5);
    EXPECT_TRUE(d.Bool("Airborne")); // 83.3 ms

    d.Set("Grounded", true);
    d.Step(6);
    EXPECT_FALSE(d.Bool("Airborne"));
    d.Set("Grounded", false);
    d.Step(7);
    EXPECT_FALSE(d.Bool("Airborne"));
}

TEST(AnimFactDerivation, TimeSinceIsCappedAtItsWindow)
{
    AnimFixture fx;
    const AnimBoundRig& rig = fx.Bound(fx.DerivedRig(R"([
        { "name": "TimeSinceGrounded", "op": "time_since", "source": { "fact": "Grounded" },
          "value": true, "window_ms": 500 } ])"));
    ASSERT_TRUE(rig.Valid) << Describe(rig);
    Driver d(rig);

    d.Step(0);
    EXPECT_FLOAT_EQ(d.Float("TimeSinceGrounded"), 0.5f); // never grounded: the cap

    d.Set("Grounded", true);
    d.Step(1);
    EXPECT_FLOAT_EQ(d.Float("TimeSinceGrounded"), 0.0f);
    d.Set("Grounded", false);
    d.Step(7);
    EXPECT_NEAR(d.Float("TimeSinceGrounded"), 0.1f, 1e-5f);
    d.Step(100);
    EXPECT_FLOAT_EQ(d.Float("TimeSinceGrounded"), 0.5f);
}

TEST(AnimFactDerivation, HysteresisRisesAtEnterAndFallsAtExit)
{
    AnimFixture fx;
    const AnimBoundRig& rig = fx.Bound(fx.DerivedRig(R"([
        { "name": "Moving", "op": "hysteresis", "source": { "fact": "Speed" },
          "enter": 1.0, "exit": 0.5 } ])"));
    ASSERT_TRUE(rig.Valid) << Describe(rig);
    Driver d(rig);

    const std::vector<std::pair<float, bool>> sequence{
        { 0.0f, false }, { 0.8f, false }, { 1.0f, true }, { 0.7f, true },
        { 0.51f, true }, { 0.5f, false }, { 0.9f, false } };
    AnimTick t = 0;
    for (const auto& [speed, moving] : sequence)
    {
        d.Set("Speed", speed);
        d.Step(t++);
        EXPECT_EQ(d.Bool("Moving"), moving) << "speed " << speed;
    }
}

TEST(AnimFactDerivation, SmoothConvergesAtItsTimeConstant)
{
    AnimFixture fx;
    const AnimBoundRig& rig = fx.Bound(fx.DerivedRig(R"([
        { "name": "SmoothSpeed", "op": "smooth", "source": { "fact": "Speed" },
          "window_ms": 100 } ])"));
    ASSERT_TRUE(rig.Valid) << Describe(rig);
    Driver d(rig);

    d.Step(0);
    EXPECT_FLOAT_EQ(d.Float("SmoothSpeed"), 0.0f);
    d.Set("Speed", 10.0f);
    for (AnimTick t = 1; t <= 6; ++t)
        d.Step(t);
    // Six ticks is 100 ms, one time constant: 1 - e^-1 of the way.
    EXPECT_NEAR(d.Float("SmoothSpeed"), 10.0f * (1.0f - std::exp(-1.0f)), 1e-3f);
}

TEST(AnimFactDerivation, CompareAndCombinators)
{
    AnimFixture fx;
    const AnimBoundRig& rig = fx.Bound(fx.DerivedRig(R"([
        { "name": "Fast", "op": "compare", "source": { "fact": "Speed" },
          "compare": "ge", "constant": 5 },
        { "name": "Sprinting", "op": "and",
          "sources": [ { "fact": "Fast" }, { "fact": "Grounded" }, { "fact": "Dead", "not": true } ] },
        { "name": "Busy", "op": "or", "sources": [ { "fact": "Fast" }, { "fact": "Dead" } ] },
        { "name": "Calm", "op": "not", "source": { "fact": "Busy" } } ])"));
    ASSERT_TRUE(rig.Valid) << Describe(rig);
    Driver d(rig);

    d.Set("Speed", 5.0f);
    d.Set("Grounded", true);
    d.Step(0);
    EXPECT_TRUE(d.Bool("Fast"));
    EXPECT_TRUE(d.Bool("Sprinting"));
    EXPECT_TRUE(d.Bool("Busy"));
    EXPECT_FALSE(d.Bool("Calm"));

    d.Set("Dead", true);
    d.Set("Speed", 1.0f);
    d.Step(1);
    EXPECT_FALSE(d.Bool("Sprinting"));
    EXPECT_TRUE(d.Bool("Busy"));

    d.Set("Dead", false);
    d.Step(2);
    EXPECT_TRUE(d.Bool("Calm"));
}

// An observer that starts late agrees with one that watched from the start
// once one horizon has passed: what late join depends on.
TEST(AnimFactDerivation, ALateObserverIsExactAfterOneHorizon)
{
    AnimFixture fx;
    const AnimBoundRig& rig = fx.Bound(fx.DerivedRig(R"([
        { "name": "JustLanded", "op": "edge", "source": { "fact": "Grounded" },
          "direction": "rising", "window_ms": 120 },
        { "name": "Airborne", "op": "min_duration",
          "source": { "fact": "Grounded", "not": true }, "window_ms": 80 },
        { "name": "TimeSinceGrounded", "op": "time_since", "source": { "fact": "Grounded" },
          "value": true, "window_ms": 400 },
        { "name": "Moving", "op": "hysteresis", "source": { "fact": "Speed" },
          "enter": 1.0, "exit": 0.5 } ])"));
    ASSERT_TRUE(rig.Valid) << Describe(rig);
    const AnimTick horizonTicks =
        static_cast<AnimTick>(std::ceil(rig.HorizonMs / (kTick * 1000.0)));

    Driver early(rig);
    Driver late(rig);
    constexpr AnimTick kJoin = 37;
    const auto grounded = [](AnimTick t) { return (t / 11) % 3 != 0; };
    const auto speed = [](AnimTick t) { return static_cast<float>((t * 7) % 13) / 6.0f; };

    for (AnimTick t = 0; t < kJoin + horizonTicks + 60; ++t)
    {
        early.Set("Grounded", grounded(t));
        early.Set("Speed", speed(t));
        early.Step(t);
        if (t < kJoin)
            continue;
        late.Set("Grounded", grounded(t));
        late.Set("Speed", speed(t));
        late.Step(t);

        const bool exact = AreAnimDerivedFactsExact(rig, late.History, t, kTick);
        EXPECT_EQ(exact, t >= kJoin + horizonTicks) << t;
        if (!exact)
            continue;
        for (const AnimBoundDerivation& derivation : rig.Derivations)
        {
            if (derivation.Op == AnimDerivationOp::Hysteresis)
                continue; // a latch is exact once it has crossed a threshold, not on a clock
            EXPECT_EQ(early.Values[derivation.Result], late.Values[derivation.Result])
                << rig.Slots[derivation.Result].Name << " at " << t;
        }
    }
}

TEST(AnimFactDerivation, ANewBindingGenerationStartsObservationOver)
{
    AnimFixture fx;
    const DataAssetHandle handle = fx.DerivedRig(R"([
        { "name": "Airborne", "op": "min_duration",
          "source": { "fact": "Grounded", "not": true }, "window_ms": 80 } ])");
    AnimFactHistory history;
    std::vector<std::uint32_t> values(kAnimFactsSmall, 0u);
    EvaluateAnimDerivations(fx.Bound(handle), values, history, 0, kTick);
    EvaluateAnimDerivations(fx.Bound(handle), values, history, 10, kTick);
    EXPECT_EQ(history.ObservedSinceTick, 0u);

    ASSERT_TRUE(fx.Tags().RegisterTag("game.rebind").has_value());
    EvaluateAnimDerivations(fx.Bound(handle), values, history, 20, kTick);
    EXPECT_EQ(history.ObservedSinceTick, 20u);
    EXPECT_FALSE(AnimFactToBool(values[static_cast<std::size_t>(fx.Bound(handle).FindSlot("Airborne"))]));
}

// ─── Gathering ──────────────────────────────────────────────────────────────

TEST(AnimFactGather, FillsProvidedSlotsThenDerives)
{
    AnimFixture fx;
    ASSERT_TRUE(BindMovementAnimFacts(fx.Entities.GetResource<AnimFactProviders>()));
    const DataAssetHandle rig = fx.DerivedRig(R"([
        { "name": "Falling", "op": "compare", "source": { "fact": "VerticalSpeed" },
          "compare": "lt", "constant": 0 } ])");

    const EntityId character = fx.Entities.CreateEntity();
    fx.Entities.AddComponent(character, AnimRig{ rig });
    fx.Entities.AddComponent(character, Stable());
    fx.Entities.AddComponent(character, KinematicState{ .Velocity = Vec3d(3.0f, -2.0f, 4.0f) });

    // The rig says what the entity carries: facts to gather into, and no history,
    // since a comparison remembers nothing.
    AnimRigCompositionSystem composition(true);
    composition.Compose(fx.Entities);
    ASSERT_NE(fx.Entities.TryGet<AnimFacts>(character), nullptr);
    EXPECT_EQ(fx.Entities.TryGet<AnimFactHistory>(character), nullptr);

    AnimFactGatherSystem gather;
    gather.Gather(fx.Entities, 3, kTick);

    const AnimBoundRig& bound = fx.Bound(rig);
    const AnimFacts& facts = *fx.Entities.TryGet<AnimFacts>(character);
    const auto at = [&](std::string_view slot) {
        return facts.Values[static_cast<std::size_t>(bound.FindSlot(slot))];
    };
    EXPECT_TRUE(AnimFactToBool(at("Grounded")));
    EXPECT_FLOAT_EQ(AnimFactToFloat(at("Speed")), 5.0f);
    EXPECT_FLOAT_EQ(AnimFactToFloat(at("VerticalSpeed")), -2.0f);
    EXPECT_TRUE(AnimFactToBool(at("Falling")));
}

TEST(AnimFactGather, AnInvalidRigGathersNothing)
{
    AnimFixture fx;
    ASSERT_TRUE(BindMovementAnimFacts(fx.Entities.GetResource<AnimFactProviders>()));
    const DataAssetHandle rig = fx.Rig(R"({
        "extends": "asset://animation/engine.facts.sdata",
        "slots": [ { "name": "Grounded", "kind": "bool" } ] })");

    const EntityId character = fx.Entities.CreateEntity();
    fx.Entities.AddComponent(character, AnimRig{ rig });
    fx.Entities.AddComponent(character, AnimFacts{});
    fx.Entities.AddComponent(character, Stable());

    AnimFactGatherSystem gather;
    gather.Gather(fx.Entities, 0, kTick);
    EXPECT_EQ(fx.Entities.TryGet<AnimFacts>(character)->Values[0], 0u);
}
