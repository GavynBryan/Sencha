// What a verb's kind and classification add to dispatch: a lease is acquired for an
// owner and released by it, an instant verb is never held, and a cosmetic producer
// cannot reach a gameplay operation.

#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
struct Declared
{
    std::string Name;
    VerbKind Kind = VerbKind::Instant;
    AuthoredScope Scope = AuthoredScope::Gameplay;
    AuthoredPrediction Prediction = AuthoredPrediction::AuthorityOnly;
};

bool Declare(VerbRegistry& registry, const Declared& declared)
{
    VerbDefinition definition;
    definition.Name = declared.Name;
    definition.Kind = declared.Kind;
    definition.Scope = declared.Scope;
    definition.Prediction = declared.Prediction;
    VerbRegistrationScope scope(registry, "test");
    (void)scope.Declare(std::move(definition));
    return scope.Commit();
}

// Records every call, which is what a lease provider's two halves look like from
// outside: an acquire with arguments, a release with only the owner.
class RecordingOperation
{
public:
    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        Calls.push_back({ invocation.Step, invocation.Owner, invocation.Arguments != nullptr });
        if (Reenter != nullptr)
            ReentrantStatus = Reenter->ReleaseLease(invocation.Verb, invocation.Owner);
        return VerbAdmission::Accepted;
    }

    struct Call
    {
        LeaseStep Step = LeaseStep::None;
        LeaseOwner Owner;
        bool HadArguments = false;
    };

    std::vector<Call> Calls;
    VerbDispatcher* Reenter = nullptr;
    VerbAdmission ReentrantStatus = VerbAdmission::Accepted;
};

class VerbLeaseTest : public ::testing::Test
{
protected:
    CompiledVerbBinding Compile(std::string_view verb)
    {
        VerbBindingDesc desc;
        desc.Key = std::string(verb) + ".binding";
        desc.KeyId = MakeVerbBindingKey(desc.Key);
        desc.VerbName = std::string(verb);
        CompiledVerbBinding compiled;
        std::vector<std::string> errors;
        EXPECT_TRUE(CompileVerbBinding(desc, VerbBindingEnvironment{ .Verbs = &Verbs }, compiled, errors));
        return compiled;
    }

    VerbRegistry Verbs;
    const LeaseOwner Holder{ .Entity = EntityId{ 4, 1 }, .Key = 77 };
};
}

TEST_F(VerbLeaseTest, ALeaseIsAcquiredForItsOwnerAndReleasedByIt)
{
    ASSERT_TRUE(Declare(Verbs, { .Name = "test.hold", .Kind = VerbKind::Lease }));
    VerbDispatcher dispatcher(Verbs);
    VerbTraceRing trace(8);
    dispatcher.SetTrace(&trace);
    RecordingOperation operation;
    const VerbId hold = Verbs.Find("test.hold");
    const VerbBindingToken token = dispatcher.Bind(hold, operation);

    const CompiledVerbBinding binding = Compile("test.hold");
    EXPECT_TRUE(dispatcher.AcquireLease(binding, {}, Holder).Accepted());
    EXPECT_EQ(dispatcher.ReleaseLease(hold, Holder), VerbAdmission::Accepted);

    ASSERT_EQ(operation.Calls.size(), 2u);
    EXPECT_EQ(operation.Calls[0].Step, LeaseStep::Acquire);
    EXPECT_EQ(operation.Calls[0].Owner, Holder);
    EXPECT_TRUE(operation.Calls[0].HadArguments);
    EXPECT_EQ(operation.Calls[1].Step, LeaseStep::Release);
    EXPECT_EQ(operation.Calls[1].Owner, Holder);
    EXPECT_FALSE(operation.Calls[1].HadArguments) << "a release names only its owner";
    const std::vector<VerbTraceRecord> records = trace.Snapshot();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[1].Event, VerbTraceEvent::Released);
}

TEST_F(VerbLeaseTest, KindsAreNotInterchangeable)
{
    ASSERT_TRUE(Declare(Verbs, { .Name = "test.hold", .Kind = VerbKind::Lease }));
    ASSERT_TRUE(Declare(Verbs, { .Name = "test.once" }));
    VerbDispatcher dispatcher(Verbs);
    RecordingOperation operation;
    const VerbBindingToken holdToken = dispatcher.Bind(Verbs.Find("test.hold"), operation);
    const VerbBindingToken onceToken = dispatcher.Bind(Verbs.Find("test.once"), operation);

    EXPECT_EQ(dispatcher.Invoke(Compile("test.hold"), {}).Status, VerbAdmission::KindMismatch);
    EXPECT_EQ(dispatcher.AcquireLease(Compile("test.once"), {}, Holder).Status, VerbAdmission::KindMismatch);
    EXPECT_EQ(dispatcher.ReleaseLease(Verbs.Find("test.once"), Holder), VerbAdmission::KindMismatch);
    EXPECT_EQ(dispatcher.AcquireLease(Compile("test.hold"), {}, LeaseOwner{}).Status,
              VerbAdmission::InvalidArguments)
        << "a hold nobody owns could never be released";
    EXPECT_TRUE(operation.Calls.empty());
}

TEST_F(VerbLeaseTest, AReleaseFromInsideADispatchIsRefused)
{
    ASSERT_TRUE(Declare(Verbs, { .Name = "test.hold", .Kind = VerbKind::Lease }));
    VerbDispatcher dispatcher(Verbs);
    RecordingOperation operation;
    operation.Reenter = &dispatcher;
    const VerbBindingToken token = dispatcher.Bind(Verbs.Find("test.hold"), operation);

    EXPECT_TRUE(dispatcher.AcquireLease(Compile("test.hold"), {}, Holder).Accepted());
    EXPECT_EQ(operation.ReentrantStatus, VerbAdmission::Reentrant);
    EXPECT_EQ(operation.Calls.size(), 1u);
}

TEST_F(VerbLeaseTest, AnUnboundLeaseHasNothingToRelease)
{
    ASSERT_TRUE(Declare(Verbs, { .Name = "test.hold", .Kind = VerbKind::Lease }));
    VerbDispatcher dispatcher(Verbs);
    EXPECT_EQ(dispatcher.ReleaseLease(Verbs.Find("test.hold"), Holder), VerbAdmission::Unavailable);
}

TEST_F(VerbLeaseTest, ScopeMismatchIsRefusedAtRuntime)
{
    ASSERT_TRUE(Declare(Verbs, { .Name = "test.damage" }));
    ASSERT_TRUE(Declare(Verbs, { .Name = "test.sparkle",
                                 .Scope = AuthoredScope::Cosmetic,
                                 .Prediction = AuthoredPrediction::Presentation }));
    VerbDispatcher dispatcher(Verbs);
    RecordingOperation operation;
    const VerbBindingToken damage = dispatcher.Bind(Verbs.Find("test.damage"), operation);
    const VerbBindingToken sparkle = dispatcher.Bind(Verbs.Find("test.sparkle"), operation);
    const VerbInvocationSource cosmetic{ .Scope = AuthoredScope::Cosmetic };

    EXPECT_EQ(dispatcher.Invoke(Compile("test.damage"), {}, cosmetic).Status, VerbAdmission::ScopeMismatch);
    EXPECT_TRUE(operation.Calls.empty()) << "presentation never reaches a gameplay operation";
    EXPECT_TRUE(dispatcher.Invoke(Compile("test.sparkle"), {}, cosmetic).Accepted());
    EXPECT_TRUE(dispatcher.Invoke(Compile("test.sparkle"), {}).Accepted())
        << "gameplay may ask for presentation";
}

TEST_F(VerbLeaseTest, AClassificationThatContradictsItselfIsRefused)
{
    EXPECT_FALSE(Declare(Verbs, { .Name = "test.predicted_sparkle",
                                  .Scope = AuthoredScope::Cosmetic,
                                  .Prediction = AuthoredPrediction::Simulation }));
    EXPECT_FALSE(Declare(Verbs, { .Name = "test.presented_damage", .Prediction = AuthoredPrediction::Presentation }));
    EXPECT_EQ(Verbs.InstallationErrors().size(), 2u);
    EXPECT_FALSE(Verbs.Find("test.predicted_sparkle").IsValid());
}

TEST_F(VerbLeaseTest, KindAndClassificationArePartOfTheContract)
{
    ASSERT_TRUE(Declare(Verbs, { .Name = "test.hold", .Kind = VerbKind::Lease }));
    const VerbId hold = Verbs.Find("test.hold");
    const VerbContractRevision before = Verbs.Revision(hold);
    const CompiledVerbBinding stale = Compile("test.hold");

    ASSERT_TRUE(Declare(Verbs, { .Name = "test.hold",
                                 .Kind = VerbKind::Lease,
                                 .Prediction = AuthoredPrediction::Simulation }));
    EXPECT_NE(Verbs.Revision(hold), before);
    VerbDispatcher dispatcher(Verbs);
    RecordingOperation operation;
    const VerbBindingToken token = dispatcher.Bind(hold, operation);
    EXPECT_EQ(dispatcher.AcquireLease(stale, {}, Holder).Status, VerbAdmission::StaleBinding);
}
