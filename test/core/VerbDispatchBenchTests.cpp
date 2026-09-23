// What a warmed invocation costs, and what it must not do.
//
// The numbers are printed, not asserted: a Debug build's timing is a
// representative measurement for the handoff, not a gate. What is asserted is
// the shape the design promises -- a no-argument invocation through a compiled
// binding allocates nothing once the dispatcher has seen the binding's width,
// and an entity-argument invocation pays for copying the argument pack and
// nothing else.

#include "AllocationCounter.h"

#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>

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
