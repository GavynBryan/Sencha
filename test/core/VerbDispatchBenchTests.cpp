// What a warmed invocation costs, and what it must not do.
//
// The numbers are printed, not asserted: a Debug build's timing is a
// representative measurement for the handoff, not a gate. What is asserted is
// the shape the design promises -- a no-argument invocation through a compiled
// binding allocates nothing once the dispatcher has seen the binding's width,
// and an entity-argument invocation pays for copying the argument pack and
// nothing else.

#include "AllocationCounter.h"
#include "component_codegen/GoldenAuthoredApi.h"
#include "component_codegen/GoldenAuthoredApi.sencha.h.expected"

#include <authored/AuthoredApi.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
class NullOperation
{
public:
    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        EntityId entity;
        Sum += invocation.Arguments->TryGetEntity(0, entity) ? entity.Index : 0;
        return VerbAdmission::Accepted;
    }
    std::uint64_t Sum = 0;
};

[[nodiscard]] bool Declare(VerbRegistry& registry, std::string name, DataFieldSchema arguments)
{
    VerbDefinition definition;
    definition.Name = std::move(name);
    definition.Arguments = std::move(arguments);
    VerbRegistrationScope scope(registry, "bench");
    (void)scope.Declare(std::move(definition));
    return scope.Commit();
}

double NanosPerCall(auto&& call, int iterations)
{
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i)
        call();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count() / iterations;
}
}

TEST(VerbDispatchBench, WarmedInvocationsAllocateNothingForNoArgumentsAndNothingExtraForAnEntity)
{
    VerbRegistry registry;
    ASSERT_TRUE(Declare(registry, "bench.none", EmptyVerbArguments()));
    DataFieldSchema target;
    target.Key = "Target";
    target.Kind = DataFieldKind::Entity;
    DataFieldSchema root = EmptyVerbArguments();
    root.Children.push_back(std::move(target));
    ASSERT_TRUE(Declare(registry, "bench.entity", std::move(root)));

    VerbBindingDesc noneDesc;
    noneDesc.Key = "none";
    noneDesc.KeyId = MakeVerbBindingKey(noneDesc.Key);
    noneDesc.VerbName = "bench.none";

    VerbBindingDesc entityDesc;
    entityDesc.Key = "entity";
    entityDesc.KeyId = MakeVerbBindingKey(entityDesc.Key);
    entityDesc.VerbName = "bench.entity";
    entityDesc.Inputs = { "target" };
    VerbBindingArgument fromInput;
    fromInput.Key = "Target";
    fromInput.Source = VerbArgumentSource::Input;
    fromInput.Text = "target";
    entityDesc.Arguments.push_back(std::move(fromInput));

    std::vector<std::string> errors;
    CompiledVerbBinding none;
    CompiledVerbBinding entity;
    const VerbBindingEnvironment environment{ .Verbs = &registry };
    ASSERT_TRUE(CompileVerbBinding(noneDesc, environment, none, errors));
    ASSERT_TRUE(CompileVerbBinding(entityDesc, environment, entity, errors));

    VerbDispatcher dispatcher(registry);
    NullOperation operation;
    const VerbBindingToken noneToken = dispatcher.Bind(none.Verb, operation);
    const VerbBindingToken entityToken = dispatcher.Bind(entity.Verb, operation);
    const AuthoredValue value = AuthoredValue::Entity(EntityId{ .Index = 3, .Generation = 1 });

    // Warm: the first call of each shape sizes the dispatcher's scratch pack.
    ASSERT_TRUE(dispatcher.Invoke(none, {}).Accepted());
    ASSERT_TRUE(dispatcher.Invoke(entity, { &value, 1 }).Accepted());

    constexpr int kIterations = 200000;

    const std::size_t beforeNone = AllocationCount();
    const double noneNanos =
        NanosPerCall([&] { (void)dispatcher.Invoke(none, {}); }, kIterations);
    EXPECT_EQ(AllocationCount(), beforeNone) << "a no-argument invocation allocated after warm-up";

    const std::size_t beforeEntity = AllocationCount();
    const double entityNanos = NanosPerCall(
        [&] { (void)dispatcher.Invoke(entity, { &value, 1 }); }, kIterations);
    // An entity is a scalar in the pack; copying it allocates nothing either.
    EXPECT_EQ(AllocationCount(), beforeEntity) << "an entity-argument invocation allocated after warm-up";

    std::printf("[bench] warmed dispatch: no-argument %.0f ns/call, entity-argument %.0f ns/call "
                "(%d iterations each, %s)\n",
                noneNanos, entityNanos, kIterations,
#ifdef NDEBUG
                "release");
#else
                "debug");
#endif
    EXPECT_GT(operation.Sum, 0u);
}

namespace
{
class HandPaint
{
public:
    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        const AuthoredArguments& arguments = *invocation.Arguments;
        EntityId target;
        std::string_view label;
        const AssetRef* decal = nullptr;
        double scale = 0.0;
        std::int64_t layer = 0;
        if (!arguments.TryGetEntity(0, target) || !Entities->IsAlive(target) || !arguments.TryGetString(1, label)
            || !arguments.TryGetAsset(2, decal) || !arguments.TryGetFloat(3, scale) || !std::isfinite(scale)
            || std::fabs(scale) > static_cast<double>(std::numeric_limits<float>::max())
            || !arguments.TryGetInt(4, layer) || !std::in_range<std::int32_t>(layer))
            return VerbAdmission::InvalidArguments;
        Sum += target.Index + label.size() + static_cast<std::uint64_t>(decal->IsValid())
             + static_cast<std::uint64_t>(static_cast<float>(scale)) + static_cast<std::uint64_t>(layer);
        return VerbAdmission::Accepted;
    }

    const World* Entities = nullptr;
    std::uint64_t Sum = 0;
};

[[nodiscard]] VerbBindingDesc PaintBinding(std::string verb)
{
    VerbBindingDesc desc;
    desc.Key = verb + ".binding";
    desc.KeyId = MakeVerbBindingKey(desc.Key);
    desc.VerbName = std::move(verb);
    desc.Inputs = { "target" };
    VerbBindingArgument target;
    target.Key = "target";
    target.Source = VerbArgumentSource::Input;
    target.Text = "target";
    VerbBindingArgument label;
    label.Key = "label";
    label.Literal = JsonValue(std::string("a label longer than any small-string buffer holds"));
    VerbBindingArgument decal;
    decal.Key = "decal";
    decal.Source = VerbArgumentSource::Asset;
    decal.Text = "asset://textures/decal.stex";
    VerbBindingArgument scale;
    scale.Key = "scale";
    scale.Literal = JsonValue(2.0);
    VerbBindingArgument layer;
    layer.Key = "layer";
    layer.Literal = JsonValue(3.0);
    desc.Arguments = { target, label, decal, scale, layer };
    return desc;
}
}

TEST(VerbDispatchBench, AGeneratedAdapterReadsAStringAndAnAssetInPlaceAtTheHandReadersCost)
{
    World world;
    InstallAuthoredVocabulary(world);
    {
        AuthoredVocabularyScope generated(world, "bench.generated");
        generated.Declare<GoldenBenchVerbs>();
        ASSERT_TRUE(generated.Commit());
    }
    VerbRegistry& registry = *FindVerbRegistry(world);
    VerbDefinition byHand = AuthoredApiDefinition<GoldenBenchVerbs>::Verbs[1].Describe();
    byHand.Name = "bench.paint_by_hand";
    {
        VerbRegistrationScope scope(registry, "bench.hand");
        (void)scope.Declare(std::move(byHand));
        ASSERT_TRUE(scope.Commit());
    }

    std::vector<std::string> errors;
    CompiledVerbBinding generatedBinding;
    CompiledVerbBinding handBinding;
    const VerbBindingEnvironment environment = MakeVerbBindingEnvironment(world);
    ASSERT_TRUE(CompileVerbBinding(PaintBinding("test.bench.paint"), environment, generatedBinding, errors))
        << (errors.empty() ? std::string{} : errors.front());
    ASSERT_TRUE(CompileVerbBinding(PaintBinding("bench.paint_by_hand"), environment, handBinding, errors))
        << (errors.empty() ? std::string{} : errors.front());

    VerbDispatcher dispatcher(registry);
    GoldenBenchVerbs generated;
    generated.Entities = &world;
    HandPaint hand;
    hand.Entities = &world;
    AuthoredApiBindings generatedTokens = BindAuthoredApi(&dispatcher, nullptr, generated);
    const VerbBindingToken handToken = dispatcher.Bind(handBinding.Verb, hand);
    const AuthoredValue target = AuthoredValue::Entity(world.CreateEntity());

    ASSERT_TRUE(dispatcher.Invoke(generatedBinding, { &target, 1 }).Accepted());
    ASSERT_TRUE(dispatcher.Invoke(handBinding, { &target, 1 }).Accepted());
    ASSERT_EQ(generated.Sum, hand.Sum);

    constexpr int kIterations = 200000;
    constexpr int kRounds = 5;
    double generatedNanos = 0.0;
    double handNanos = 0.0;
    std::size_t generatedAllocations = 0;
    std::size_t handAllocations = 0;
    for (int round = 0; round < kRounds; ++round)
    {
        const std::size_t beforeGenerated = AllocationCount();
        const double generatedRound =
            NanosPerCall([&] { (void)dispatcher.Invoke(generatedBinding, { &target, 1 }); }, kIterations);
        generatedAllocations += AllocationCount() - beforeGenerated;
        const std::size_t beforeHand = AllocationCount();
        const double handRound = NanosPerCall([&] { (void)dispatcher.Invoke(handBinding, { &target, 1 }); }, kIterations);
        handAllocations += AllocationCount() - beforeHand;
        generatedNanos = round == 0 ? generatedRound : std::min(generatedNanos, generatedRound);
        handNanos = round == 0 ? handRound : std::min(handNanos, handRound);
    }
    EXPECT_EQ(generatedAllocations, 0u) << "the generated adapter allocated for a string or an asset argument";
    EXPECT_EQ(handAllocations, 0u);
    EXPECT_EQ(generated.Sum, hand.Sum);

    std::printf("[bench] five-argument verb, string and asset: generated %.1f ns/call, hand %.1f ns/call "
                "(best of %d rounds of %d, %s)\n",
                generatedNanos, handNanos, kRounds, kIterations,
#ifdef NDEBUG
                "release");
#else
                "debug");
#endif
}
