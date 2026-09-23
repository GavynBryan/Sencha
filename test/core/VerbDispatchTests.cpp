// The one entry point from an authored producer to a registered operation:
// what it checks before calling, what it refuses, and what happens to a binding
// or a token that has outlived what it referred to.

#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>
#include <core/identity/Id.h>
#include <world/identity/PersistentEntityIndex.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
[[nodiscard]] DataFieldSchema Field(std::string key, DataFieldKind kind)
{
    DataFieldSchema field;
    field.Key = std::move(key);
    field.Kind = kind;
    return field;
}

[[nodiscard]] DataFieldSchema Record(std::vector<DataFieldSchema> children)
{
    DataFieldSchema root = EmptyVerbArguments();
    root.Children = std::move(children);
    return root;
}

bool Declare(VerbRegistry& registry, std::string name, DataFieldSchema arguments)
{
    VerbDefinition definition;
    definition.Name = std::move(name);
    definition.Arguments = std::move(arguments);
    VerbRegistrationScope scope(registry, "test");
    (void)scope.Declare(std::move(definition));
    return scope.Commit();
}

[[nodiscard]] VerbBindingDesc Binding(std::string key,
                                      std::string verb,
                                      std::vector<VerbBindingArgument> arguments = {},
                                      std::vector<std::string> inputs = {})
{
    VerbBindingDesc desc;
    desc.Key = std::move(key);
    desc.KeyId = MakeVerbBindingKey(desc.Key);
    desc.VerbName = std::move(verb);
    desc.Inputs = std::move(inputs);
    desc.Arguments = std::move(arguments);
    return desc;
}

[[nodiscard]] VerbBindingArgument FromInput(std::string key, std::string input)
{
    VerbBindingArgument argument;
    argument.Key = std::move(key);
    argument.Source = VerbArgumentSource::Input;
    argument.Text = std::move(input);
    return argument;
}

[[nodiscard]] VerbBindingArgument Literal(std::string key, JsonValue value)
{
    VerbBindingArgument argument;
    argument.Key = std::move(key);
    argument.Source = VerbArgumentSource::Literal;
    argument.Literal = std::move(value);
    return argument;
}

// A bounded operation that defers, which is the shape the timing rules are
// written for: it copies what it needs at admission and acts later.
class CountingOperation
{
public:
    explicit CountingOperation(std::size_t capacity = 8) : Capacity(capacity) {}

    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        if (Refuse)
            return VerbAdmission::Refused;
        if (Queue.size() >= Capacity)
            return VerbAdmission::QueueFull;
        if (invocation.Arguments == nullptr)
            return VerbAdmission::InvalidArguments;

        // The pack is borrowed for the duration of the call, so a deferred
        // operation owns a copy of what it will act on.
        Queued record;
        record.Id = invocation.Id;
        record.Parent = invocation.Parent;
        record.Binding = invocation.Binding;
        (void)invocation.Arguments->TryGetInt(0, record.Amount);
        Queue.push_back(record);
        return VerbAdmission::Accepted;
    }

    struct Queued
    {
        InvocationId Id;
        InvocationId Parent;
        VerbBindingKey Binding;
        std::int64_t Amount = 0;
    };

    std::vector<Queued> Queue;
    std::size_t Capacity;
    bool Refuse = false;
};

class VerbDispatchTest : public ::testing::Test
{
protected:
    [[nodiscard]] bool Compile(const VerbBindingDesc& desc, CompiledVerbBinding& out)
    {
        Errors.clear();
        return CompileVerbBinding(desc, VerbBindingEnvironment{ .Verbs = &Verbs }, out, Errors);
    }

    VerbRegistry Verbs;
    std::vector<std::string> Errors;
};
}

TEST_F(VerbDispatchTest, ABoundOperationReceivesTheCompiledConstantsAndTheProducerValues)
{
    ASSERT_TRUE(Declare(Verbs, "test.score",
                        Record({ Field("Amount", DataFieldKind::Int),
                                 Field("Label", DataFieldKind::String) })));

    CompiledVerbBinding constant;
    CompiledVerbBinding dynamic;
    ASSERT_TRUE(Compile(Binding("fixed", "test.score",
                                { Literal("Amount", JsonValue(7.0)),
                                  Literal("Label", JsonValue("fixed")) }),
                        constant));
    ASSERT_TRUE(Compile(Binding("live", "test.score",
                                { FromInput("Amount", "amount"),
                                  Literal("Label", JsonValue("live")) },
                                { "amount" }),
                        dynamic));

    VerbDispatcher dispatcher(Verbs);
    CountingOperation operation;
    const VerbBindingToken token = dispatcher.Bind(Verbs.Find("test.score"), operation);
    ASSERT_TRUE(token.IsValid());
    EXPECT_TRUE(dispatcher.HasImplementation(Verbs.Find("test.score")));

    EXPECT_TRUE(dispatcher.Invoke(constant, {}).Accepted());
    const AuthoredValue amount = AuthoredValue::Int(3);
    EXPECT_TRUE(dispatcher.Invoke(dynamic, { &amount, 1 }).Accepted());

    ASSERT_EQ(operation.Queue.size(), 2u);
    EXPECT_EQ(operation.Queue[0].Amount, 7);
    EXPECT_EQ(operation.Queue[0].Binding, MakeVerbBindingKey("fixed"));
    EXPECT_EQ(operation.Queue[1].Amount, 3);
    EXPECT_EQ(operation.Queue[1].Binding, MakeVerbBindingKey("live"));
}

TEST_F(VerbDispatchTest, EachRefusalIsItsOwnAnswer)
{
    ASSERT_TRUE(Declare(Verbs, "test.op", Record({ Field("Amount", DataFieldKind::Int) })));
    CompiledVerbBinding binding;
    ASSERT_TRUE(Compile(Binding("op", "test.op", { FromInput("Amount", "amount") }, { "amount" }),
                        binding));

    VerbDispatcher dispatcher(Verbs);
    const AuthoredValue amount = AuthoredValue::Int(1);

    // Declared, and nothing behind it. Discovery and execution are separate
    // facts, which is what an editor relies on.
    EXPECT_EQ(dispatcher.Invoke(binding, { &amount, 1 }).Status, VerbAdmission::Unavailable);

    CountingOperation operation;
    VerbBindingToken token = dispatcher.Bind(binding.Verb, operation);

    // The producer supplied the wrong number of values, then the wrong kind.
    EXPECT_EQ(dispatcher.Invoke(binding, {}).Status, VerbAdmission::InvalidArguments);
    const AuthoredValue text = AuthoredValue::String("three");
    EXPECT_EQ(dispatcher.Invoke(binding, { &text, 1 }).Status, VerbAdmission::InvalidArguments);

    operation.Refuse = true;
    EXPECT_EQ(dispatcher.Invoke(binding, { &amount, 1 }).Status, VerbAdmission::Refused);
    operation.Refuse = false;

    CountingOperation tiny(1);
    token = dispatcher.Bind(binding.Verb, tiny);
    EXPECT_TRUE(dispatcher.Invoke(binding, { &amount, 1 }).Accepted());
    EXPECT_EQ(dispatcher.Invoke(binding, { &amount, 1 }).Status, VerbAdmission::QueueFull);

    // Shutdown closes admission before implementations go, so nothing is
    // accepted by an operation that is about to disappear.
    dispatcher.CloseAdmission();
    EXPECT_EQ(dispatcher.Invoke(binding, { &amount, 1 }).Status, VerbAdmission::Unavailable);
}

TEST_F(VerbDispatchTest, ABindingFromAnotherCatalogOrAnOlderContractIsNotDispatched)
{
    ASSERT_TRUE(Declare(Verbs, "test.op", Record({ Field("Amount", DataFieldKind::Int) })));
    CompiledVerbBinding binding;
    ASSERT_TRUE(Compile(Binding("op", "test.op", { Literal("Amount", JsonValue(1.0)) }), binding));

    VerbDispatcher dispatcher(Verbs);
    CountingOperation operation;
    const VerbBindingToken token = dispatcher.Bind(binding.Verb, operation);
    ASSERT_TRUE(dispatcher.Invoke(binding, {}).Accepted());

    // Another World's binding, resolved to the same slot number.
    VerbRegistry other;
    ASSERT_TRUE(Declare(other, "test.op", Record({ Field("Amount", DataFieldKind::Int) })));
    CompiledVerbBinding foreign;
    std::vector<std::string> errors;
    ASSERT_TRUE(CompileVerbBinding(Binding("op", "test.op", { Literal("Amount", JsonValue(1.0)) }),
                                   VerbBindingEnvironment{ .Verbs = &other }, foreign, errors));
    EXPECT_EQ(foreign.Verb, binding.Verb);
    EXPECT_EQ(dispatcher.Invoke(foreign, {}).Status, VerbAdmission::UnresolvedBinding);

    // A contract that moved under a binding compiled against the old one.
    ASSERT_TRUE(Declare(Verbs, "test.op", Record({ Field("Points", DataFieldKind::Int) })));
    EXPECT_EQ(dispatcher.Invoke(binding, {}).Status, VerbAdmission::StaleBinding);

    // And a verb whose provider has gone.
    Verbs.RetireProvider("test");
    EXPECT_EQ(dispatcher.Invoke(binding, {}).Status, VerbAdmission::StaleBinding);
    EXPECT_EQ(operation.Queue.size(), 1u);
}

TEST_F(VerbDispatchTest, AnOldTokenCannotRemoveTheImplementationThatReplacedIt)
{
    ASSERT_TRUE(Declare(Verbs, "test.op", EmptyVerbArguments()));
    CompiledVerbBinding binding;
    ASSERT_TRUE(Compile(Binding("op", "test.op"), binding));

    VerbDispatcher dispatcher(Verbs);
    CountingOperation first;
    CountingOperation second;

    VerbBindingToken firstToken = dispatcher.Bind(binding.Verb, first);
    const VerbBindingToken secondToken = dispatcher.Bind(binding.Verb, second);

    firstToken.Reset();
    EXPECT_TRUE(dispatcher.HasImplementation(binding.Verb));
    EXPECT_TRUE(dispatcher.Invoke(binding, {}).Accepted());
    EXPECT_TRUE(first.Queue.empty());
    EXPECT_EQ(second.Queue.size(), 1u);
}

TEST_F(VerbDispatchTest, ATokenThatOutlivesItsDispatcherIsInert)
{
    ASSERT_TRUE(Declare(Verbs, "test.op", EmptyVerbArguments()));
    CountingOperation operation;
    VerbBindingToken token;
    {
        VerbDispatcher dispatcher(Verbs);
        token = dispatcher.Bind(Verbs.Find("test.op"), operation);
        EXPECT_TRUE(token.IsValid());
    }
    // The contract is chosen rather than inherited from declaration order:
    // unbinding late is safe, so a host whose token member happens to be
    // declared before its dispatcher is still correct.
    EXPECT_FALSE(token.IsValid());
    token.Reset();
}

TEST_F(VerbDispatchTest, DispatchRefusesToReenterItself)
{
    ASSERT_TRUE(Declare(Verbs, "test.outer", EmptyVerbArguments()));
    CompiledVerbBinding binding;
    ASSERT_TRUE(Compile(Binding("outer", "test.outer"), binding));

    VerbDispatcher dispatcher(Verbs);

    // An implementation that tries to invoke through the dispatcher that called
    // it. Refused whole: a partly executed recursive chain is worse than a
    // diagnostic, and an orchestrator that wants a follow-up schedules one.
    struct Recursive
    {
        VerbDispatcher* Dispatcher = nullptr;
        const CompiledVerbBinding* Binding = nullptr;
        VerbAdmission Inner = VerbAdmission::Accepted;

        VerbAdmission Invoke(const VerbInvocation&)
        {
            Inner = Dispatcher->Invoke(*Binding, {}).Status;
            return VerbAdmission::Accepted;
        }
    };

    Recursive recursive{ .Dispatcher = &dispatcher, .Binding = &binding };
    const VerbBindingToken token = dispatcher.Bind(binding.Verb, recursive);

    EXPECT_TRUE(dispatcher.Invoke(binding, {}).Accepted());
    EXPECT_EQ(recursive.Inner, VerbAdmission::Reentrant);
    // And the dispatcher is usable afterwards.
    EXPECT_TRUE(dispatcher.Invoke(binding, {}).Accepted());
}

TEST_F(VerbDispatchTest, AQueuedRequestOwnsItsPayload)
{
    ASSERT_TRUE(Declare(Verbs, "test.op", Record({ Field("Amount", DataFieldKind::Int) })));
    CompiledVerbBinding binding;
    ASSERT_TRUE(Compile(Binding("op", "test.op", { FromInput("Amount", "amount") }, { "amount" }),
                        binding));

    VerbDispatcher dispatcher(Verbs);
    CountingOperation operation;
    const VerbBindingToken token = dispatcher.Bind(binding.Verb, operation);

    for (std::int64_t value : { 1, 2, 3 })
    {
        const AuthoredValue amount = AuthoredValue::Int(value);
        EXPECT_TRUE(dispatcher.Invoke(binding, { &amount, 1 }).Accepted());
    }

    // The argument pack the dispatcher lends is reused across calls, so a record
    // that had kept a pointer into it would now read 3 three times.
    ASSERT_EQ(operation.Queue.size(), 3u);
    EXPECT_EQ(operation.Queue[0].Amount, 1);
    EXPECT_EQ(operation.Queue[1].Amount, 2);
    EXPECT_EQ(operation.Queue[2].Amount, 3);
}

TEST_F(VerbDispatchTest, IdentityIsMintedOnAcceptanceAndCarriesItsParent)
{
    ASSERT_TRUE(Declare(Verbs, "test.op", EmptyVerbArguments()));
    CompiledVerbBinding binding;
    ASSERT_TRUE(Compile(Binding("op", "test.op"), binding));

    VerbDispatcher dispatcher(Verbs);
    CountingOperation operation;
    const VerbBindingToken token = dispatcher.Bind(binding.Verb, operation);

    const VerbInvocationResult first = dispatcher.Invoke(binding, {});
    ASSERT_TRUE(first.Accepted());
    EXPECT_TRUE(first.Id.IsValid());

    // A refusal reports no id and consumes no number: the sequence tolerates a
    // gap, and the implementation still saw a candidate to file away.
    operation.Refuse = true;
    const VerbInvocationResult refused = dispatcher.Invoke(binding, {});
    EXPECT_FALSE(refused.Accepted());
    EXPECT_FALSE(refused.Id.IsValid());
    operation.Refuse = false;

    const VerbInvocationResult second =
        dispatcher.Invoke(binding, {}, VerbInvocationSource{ .Parent = first.Id, .Producer = {}, .Instigator = {}, .Tick = 0 });
    ASSERT_TRUE(second.Accepted());
    EXPECT_GT(second.Id.Value, first.Id.Value);

    ASSERT_EQ(operation.Queue.size(), 2u);
    EXPECT_EQ(operation.Queue[0].Id, first.Id);
    EXPECT_FALSE(operation.Queue[0].Parent.IsValid());
    EXPECT_EQ(operation.Queue[1].Parent, first.Id);
}

TEST_F(VerbDispatchTest, TracingCanBeTurnedOffWithoutTurningOffIdentity)
{
    ASSERT_TRUE(Declare(Verbs, "test.op", EmptyVerbArguments()));
    CompiledVerbBinding binding;
    ASSERT_TRUE(Compile(Binding("op", "test.op"), binding));

    VerbDispatcher dispatcher(Verbs);
    CountingOperation operation;
    const VerbBindingToken token = dispatcher.Bind(binding.Verb, operation);

    // No ring: no history, and every identity still minted and propagated.
    const VerbInvocationResult untraced = dispatcher.Invoke(binding, {});
    ASSERT_TRUE(untraced.Accepted());

    VerbTraceRing ring(3);
    dispatcher.SetTrace(&ring);

    const VerbInvocationResult traced =
        dispatcher.Invoke(binding, {}, VerbInvocationSource{ .Parent = untraced.Id, .Producer = {}, .Instigator = {}, .Tick = 0 });
    ASSERT_TRUE(traced.Accepted());
    operation.Refuse = true;
    (void)dispatcher.Invoke(binding, {});

    const std::vector<VerbTraceRecord> records = ring.Snapshot();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].Event, VerbTraceEvent::Admitted);
    EXPECT_EQ(records[0].Id, traced.Id);
    EXPECT_EQ(records[0].Parent, untraced.Id);
    EXPECT_EQ(records[0].Binding, MakeVerbBindingKey("op"));
    EXPECT_EQ(records[1].Event, VerbTraceEvent::Rejected);
    EXPECT_EQ(records[1].Status, VerbAdmission::Refused);
    EXPECT_EQ(ring.DroppedCount(), 0u);
}

TEST(VerbTraceRingTest, AWrappedRingReportsWhatItLost)
{
    VerbTraceRing ring(2);
    EXPECT_TRUE(ring.Snapshot().empty());

    for (std::uint64_t id : { 1u, 2u, 3u, 4u })
    {
        VerbTraceRecord record;
        record.Id = InvocationId{ id };
        ring.Record(record);
    }

    const std::vector<VerbTraceRecord> records = ring.Snapshot();
    ASSERT_EQ(records.size(), 2u);
    // Oldest first, which is the order a causality chain reads in.
    EXPECT_EQ(records[0].Id.Value, 3u);
    EXPECT_EQ(records[1].Id.Value, 4u);
    // A history that quietly lost its beginning explains the wrong thing.
    EXPECT_EQ(ring.DroppedCount(), 2u);

    ring.Clear();
    EXPECT_TRUE(ring.Snapshot().empty());
    EXPECT_EQ(ring.DroppedCount(), 0u);
}

TEST_F(VerbDispatchTest, BindingRefusesAVerbThisCatalogDoesNotDeclare)
{
    VerbDispatcher dispatcher(Verbs);
    CountingOperation operation;

    EXPECT_FALSE(dispatcher.Bind(VerbId{}, operation).IsValid());
    EXPECT_FALSE(dispatcher.Bind(VerbId{ 7 }, operation).IsValid());

    ASSERT_TRUE(Declare(Verbs, "test.op", EmptyVerbArguments()));
    Verbs.RetireProvider("test");
    EXPECT_FALSE(dispatcher.Bind(VerbId{ 1 }, operation).IsValid());
}

TEST_F(VerbDispatchTest, AnImplementationBoundAgainstAnOlderContractIsNotOfferedANewerBinding)
{
    ASSERT_TRUE(Declare(Verbs, "test.op", Record({ Field("Amount", DataFieldKind::Int) })));
    VerbDispatcher dispatcher(Verbs);
    CountingOperation operation;
    VerbBindingToken token = dispatcher.Bind(Verbs.Find("test.op"), operation);

    // The contract moves, and the content is recompiled against the new one.
    // The code reading the arguments has not said it was updated, so the new
    // layout must not reach it.
    ASSERT_TRUE(Declare(Verbs, "test.op", Record({ Field("Points", DataFieldKind::Int) })));
    CompiledVerbBinding recompiled;
    ASSERT_TRUE(Compile(Binding("op", "test.op", { Literal("Points", JsonValue(2.0)) }),
                        recompiled));
    EXPECT_EQ(dispatcher.Invoke(recompiled, {}).Status, VerbAdmission::Unavailable);
    EXPECT_TRUE(operation.Queue.empty());

    // Rebinding is the statement that it was.
    token = dispatcher.Bind(Verbs.Find("test.op"), operation);
    EXPECT_TRUE(dispatcher.Invoke(recompiled, {}).Accepted());
    EXPECT_EQ(operation.Queue.size(), 1u);
}

TEST_F(VerbDispatchTest, EveryAttemptTakesItsOwnIdSoARefusalNeverSharesOneWithAnExecution)
{
    ASSERT_TRUE(Declare(Verbs, "test.op", EmptyVerbArguments()));
    CompiledVerbBinding binding;
    ASSERT_TRUE(Compile(Binding("op", "test.op"), binding));

    VerbDispatcher dispatcher(Verbs);
    VerbTraceRing ring(8);
    dispatcher.SetTrace(&ring);
    CountingOperation operation;
    const VerbBindingToken token = dispatcher.Bind(binding.Verb, operation);

    operation.Refuse = true;
    (void)dispatcher.Invoke(binding, {});
    operation.Refuse = false;
    const VerbInvocationResult accepted = dispatcher.Invoke(binding, {});
    ASSERT_TRUE(accepted.Accepted());

    const std::vector<VerbTraceRecord> records = ring.Snapshot();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].Event, VerbTraceEvent::Rejected);
    EXPECT_EQ(records[1].Event, VerbTraceEvent::Admitted);
    EXPECT_NE(records[0].Id, records[1].Id);
    EXPECT_EQ(records[1].Id, accepted.Id);
}

TEST_F(VerbDispatchTest, AnEntityConstantResolvesAtEachInvocationAgainstTheLiveIndex)
{
    ASSERT_TRUE(Declare(Verbs, "test.target", Record({ Field("Target", DataFieldKind::Entity) })));
    const PersistentEntityId identity{ 0xabcdull };
    VerbBindingArgument anchor;
    anchor.Key = "Target";
    anchor.Source = VerbArgumentSource::Entity;
    anchor.Text = PersistentEntityIdToString(identity);
    CompiledVerbBinding binding;
    ASSERT_TRUE(Compile(Binding("target", "test.target", { anchor }), binding));

    // Reads the resolved handle out of the pack.
    struct TargetOperation
    {
        std::vector<EntityId> Seen;
        VerbAdmission Invoke(const VerbInvocation& invocation)
        {
            EntityId target;
            if (!invocation.Arguments->TryGetEntity(0, target))
                return VerbAdmission::InvalidArguments;
            Seen.push_back(target);
            return VerbAdmission::Accepted;
        }
    };

    VerbDispatcher dispatcher(Verbs);
    PersistentEntityIndex index;
    dispatcher.SetEntityIndex(&index);
    TargetOperation operation;
    const VerbBindingToken token = dispatcher.Bind(binding.Verb, operation);

    // Nothing carries the identity yet: refused, and the binding is intact for
    // the next attempt rather than compiled away.
    EXPECT_EQ(dispatcher.Invoke(binding, {}).Status, VerbAdmission::UnresolvedReference);

    const EntityId first{ .Index = 4, .Generation = 1 };
    ASSERT_TRUE(index.Register(identity, first));
    ASSERT_TRUE(dispatcher.Invoke(binding, {}).Accepted());

    // Streamed out and back with a new generation: the same binding reaches
    // the new incarnation, and the operation never saw the identity itself.
    index.Unregister(identity, first);
    const EntityId second{ .Index = 4, .Generation = 2 };
    ASSERT_TRUE(index.Register(identity, second));
    ASSERT_TRUE(dispatcher.Invoke(binding, {}).Accepted());

    ASSERT_EQ(operation.Seen.size(), 2u);
    EXPECT_EQ(operation.Seen[0], first);
    EXPECT_EQ(operation.Seen[1], second);

    // A dispatcher with no index has nothing to resolve against.
    dispatcher.SetEntityIndex(nullptr);
    EXPECT_EQ(dispatcher.Invoke(binding, {}).Status, VerbAdmission::UnresolvedReference);
}

TEST_F(VerbDispatchTest, OneProducerValueFillsEveryArgumentThatNamesItsInput)
{
    ASSERT_TRUE(Declare(Verbs, "test.self",
                        Record({ Field("Source", DataFieldKind::Entity),
                                 Field("Target", DataFieldKind::Entity) })));
    CompiledVerbBinding binding;
    ASSERT_TRUE(Compile(Binding("self", "test.self",
                                { FromInput("Source", "self"), FromInput("Target", "self") },
                                { "self" }),
                        binding));

    struct PairOperation
    {
        EntityId Source;
        EntityId Target;
        VerbAdmission Invoke(const VerbInvocation& invocation)
        {
            (void)invocation.Arguments->TryGetEntity(0, Source);
            (void)invocation.Arguments->TryGetEntity(1, Target);
            return VerbAdmission::Accepted;
        }
    };

    VerbDispatcher dispatcher(Verbs);
    PairOperation operation;
    const VerbBindingToken token = dispatcher.Bind(binding.Verb, operation);
    const AuthoredValue self = AuthoredValue::Entity(EntityId{ .Index = 9, .Generation = 1 });
    ASSERT_TRUE(dispatcher.Invoke(binding, { &self, 1 }).Accepted());
    EXPECT_EQ(operation.Source, operation.Target);
    EXPECT_EQ(operation.Source.Index, 9u);
}

TEST_F(VerbDispatchTest, ProvenanceReachesTheOperationAndTheTrace)
{
    ASSERT_TRUE(Declare(Verbs, "test.op", EmptyVerbArguments()));
    CompiledVerbBinding binding;
    ASSERT_TRUE(Compile(Binding("op", "test.op"), binding));

    struct ProvenanceOperation
    {
        EntityId Instigator;
        std::uint64_t Tick = 0;
        VerbAdmission Invoke(const VerbInvocation& invocation)
        {
            Instigator = invocation.Instigator;
            Tick = invocation.Tick;
            return VerbAdmission::Accepted;
        }
    };

    VerbDispatcher dispatcher(Verbs);
    VerbTraceRing ring(4);
    dispatcher.SetTrace(&ring);
    ProvenanceOperation operation;
    const VerbBindingToken token = dispatcher.Bind(binding.Verb, operation);

    const EntityId player{ .Index = 21, .Generation = 2 };
    ASSERT_TRUE(dispatcher
                    .Invoke(binding, {},
                            VerbInvocationSource{ .Parent = {}, .Producer = {}, .Instigator = player, .Tick = 77 })
                    .Accepted());
    EXPECT_EQ(operation.Instigator, player);
    EXPECT_EQ(operation.Tick, 77u);
    ASSERT_EQ(ring.Snapshot().size(), 1u);
    EXPECT_EQ(ring.Snapshot()[0].Instigator, player);

    // A producer with nothing to say passes nothing, and nothing is invented.
    ASSERT_TRUE(dispatcher.Invoke(binding, {}).Accepted());
    EXPECT_FALSE(operation.Instigator.IsValid());
    EXPECT_EQ(operation.Tick, 0u);
}
