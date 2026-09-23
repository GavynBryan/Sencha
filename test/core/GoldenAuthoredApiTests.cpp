// The generator's expected authored-API output, compiled and run.
//
// GoldenAuthoredApi.sencha.h.expected is what sencha-component-codegen must
// emit for GoldenAuthoredApi.h; scripts/check_component_codegen.sh compares a
// real run against it byte for byte. Here the same text is compiled as C++ and
// driven through the explicit Declare and Bind calls, the real dispatchers, and
// a real World: a contract that reads right but decodes wrong fails here.

#include "component_codegen/GoldenAuthoredApi.h"
#include "component_codegen/GoldenAuthoredApi.sencha.h.expected"

#include <authored/AuthoredApi.h>
#include <authored/AuthoredEventDispatcher.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/WorldVocabulary.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

// The provider's behaviour: ordinary member functions recording what they were
// handed, which is all an adapter test needs to see.
namespace
{
struct IgniteCall
{
    InvocationId Id{};
    EntityId Instigator{};
    EntityId Torch{};
    GoldenLamp Brightness = GoldenLamp::Off;
    float Seconds = 0.0f;
    std::optional<EntityId> Named{};
};

struct GoldenTorchLog
{
    std::vector<IgniteCall> Ignites;
    int Extinguishes = 0;
    std::vector<std::string> Names;
};

GoldenTorchLog& Log()
{
    static GoldenTorchLog log;
    return log;
}
} // namespace

VerbAdmission GoldenTorchSystem::Ignite(const VerbInvocation& invocation,
                                        EntityId torch,
                                        GoldenLamp brightness,
                                        float seconds,
                                        std::optional<EntityId> instigator)
{
    Log().Ignites.push_back(IgniteCall{
        .Id = invocation.Id,
        .Instigator = invocation.Instigator,
        .Torch = torch,
        .Brightness = brightness,
        .Seconds = seconds,
        .Named = instigator,
    });
    return VerbAdmission::Accepted;
}

VerbAdmission GoldenTorchSystem::ExtinguishAll()
{
    ++Log().Extinguishes;
    return VerbAdmission::Accepted;
}

VerbAdmission GoldenTorchSystem::Rename(EntityId, const std::string& name)
{
    Log().Names.push_back(name);
    return VerbAdmission::Accepted;
}

bool GoldenTorchSystem::CanIgnite(EntityId, std::int32_t level) const
{
    return level >= 3;
}

std::optional<double> GoldenTorchSystem::BurnTime(EntityId torch) const
{
    if (torch.Index == 7)
        return std::nullopt;
    return 12.5;
}

void GoldenTorchSystem::Tick() {}

namespace
{
[[nodiscard]] VerbBindingDesc Binding(std::string verb,
                                      std::vector<VerbBindingArgument> arguments,
                                      std::vector<std::string> inputs = {})
{
    VerbBindingDesc desc;
    desc.Key = verb + ".binding";
    desc.KeyId = MakeVerbBindingKey(desc.Key);
    desc.VerbName = std::move(verb);
    desc.Arguments = std::move(arguments);
    desc.Inputs = std::move(inputs);
    return desc;
}

[[nodiscard]] VerbBindingArgument Literal(std::string key, JsonValue value)
{
    VerbBindingArgument argument;
    argument.Key = std::move(key);
    argument.Source = VerbArgumentSource::Literal;
    argument.Literal = std::move(value);
    return argument;
}

[[nodiscard]] VerbBindingArgument FromInput(std::string key, std::string input)
{
    VerbBindingArgument argument;
    argument.Key = std::move(key);
    argument.Source = VerbArgumentSource::Input;
    argument.Text = std::move(input);
    return argument;
}

// A subscriber reading the payload the event declared, by slot.
struct HeardBrightness
{
    std::vector<std::string> Brightness{};

    static void Deliver(HeardBrightness& self, const AuthoredEventDelivery& delivery)
    {
        std::string_view value;
        if (delivery.Payload->TryGetEnum(1, value))
            self.Brightness.emplace_back(value);
    }
};

class GoldenAuthoredApiTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Log() = {};
        Entities.RegisterComponent<GoldenTorch>();
        InstallAuthoredVocabulary(Entities);
        AuthoredVocabularyScope vocabulary(Entities, "golden");
        vocabulary.Declare<GoldenTorch>();
        vocabulary.Declare<GoldenTorchSystem>();
        vocabulary.Declare<GoldenTorchLitEvent>();
        vocabulary.Declare<GoldenTorchExtinguishedEvent>();
        ASSERT_TRUE(vocabulary.Commit()) << ErrorsText();

        Verbs.emplace(*FindVerbRegistry(Entities));
        Queries.emplace(*FindAuthoredQueryRegistry(Entities));
    }

    [[nodiscard]] std::string ErrorsText() const
    {
        std::string text;
        for (const std::string& error : AuthoredInstallationErrors(Entities))
            text += error + "\n";
        return text;
    }

    [[nodiscard]] VerbInvocationResult Invoke(const VerbBindingDesc& desc,
                                              std::span<const AuthoredValue> inputs = {},
                                              EntityId instigator = {})
    {
        std::vector<std::string> errors;
        CompiledVerbBinding compiled;
        EXPECT_TRUE(CompileVerbBinding(desc, MakeVerbBindingEnvironment(Entities), compiled, errors))
            << (errors.empty() ? std::string{} : errors.front());
        VerbInvocationSource source;
        source.Instigator = instigator;
        return Verbs->Invoke(compiled, inputs, source);
    }

    World Entities;
    std::optional<VerbDispatcher> Verbs;
    std::optional<AuthoredQueryDispatcher> Queries;
    GoldenTorchSystem Torches;
};
} // namespace

TEST_F(GoldenAuthoredApiTest, DeclaresTheContractTheSignatureStates)
{
    const VerbRegistry& verbs = *FindVerbRegistry(Entities);
    const VerbDefinition* ignite = verbs.Get(verbs.Find("test.torch.ignite"));
    ASSERT_NE(ignite, nullptr);
    EXPECT_EQ(ignite->DisplayName, "Ignite");
    EXPECT_EQ(ignite->Category, "Test");

    // The leading invocation is provenance, not an argument.
    const std::vector<DataFieldSchema>& arguments = ignite->Arguments.Children;
    ASSERT_EQ(arguments.size(), 4u);

    EXPECT_EQ(arguments[0].Key, "torch");
    EXPECT_EQ(arguments[0].Kind, DataFieldKind::Entity);
    EXPECT_EQ(arguments[0].Reference.ComponentIdentity, "test.codegen.golden_torch");
    EXPECT_TRUE(arguments[0].Required);

    EXPECT_EQ(arguments[1].Kind, DataFieldKind::Enum);
    ASSERT_EQ(arguments[1].EnumChoices.size(), 3u);
    EXPECT_EQ(arguments[1].EnumChoices[2].Value, "bright");
    // A C++ default is the schema's default, and the argument stays required:
    // a binding may omit it, content may not make it absent.
    EXPECT_EQ(arguments[1].Default, DataDefaultValue(std::string("bright")));
    EXPECT_TRUE(arguments[1].Required);

    EXPECT_EQ(arguments[2].Kind, DataFieldKind::Float);
    EXPECT_EQ(arguments[2].Numeric.Minimum, 0.5);
    EXPECT_EQ(arguments[2].Numeric.Maximum, 60.0);
    EXPECT_EQ(arguments[2].Default, DataDefaultValue(5.0));

    // Only std::optional is optional.
    EXPECT_EQ(arguments[3].Kind, DataFieldKind::Optional);
    EXPECT_FALSE(arguments[3].Required);
    ASSERT_EQ(arguments[3].Children.size(), 1u);
    EXPECT_EQ(arguments[3].Children[0].Kind, DataFieldKind::Entity);
}

TEST_F(GoldenAuthoredApiTest, AVerbReachesTheMethodWithEveryArgumentDecoded)
{
    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    EXPECT_TRUE(bindings.Unbound().empty());
    EXPECT_EQ(bindings.BoundCount(), 5u);

    const EntityId torch = Entities.CreateEntity();
    const EntityId player = Entities.CreateEntity();
    const AuthoredValue input = AuthoredValue::Entity(torch);
    const VerbInvocationResult result = Invoke(
        Binding("test.torch.ignite", { FromInput("torch", "which"), Literal("brightness", "dim") },
                { "which" }),
        { &input, 1 }, player);

    ASSERT_EQ(result.Status, VerbAdmission::Accepted);
    ASSERT_EQ(Log().Ignites.size(), 1u);
    const IgniteCall& call = Log().Ignites.front();
    EXPECT_EQ(call.Torch, torch);
    EXPECT_EQ(call.Brightness, GoldenLamp::Dim);
    // Unfilled, so the declared default.
    EXPECT_FLOAT_EQ(call.Seconds, 5.0f);
    EXPECT_FALSE(call.Named.has_value());
    // The provenance the leading parameter asked for.
    EXPECT_EQ(call.Id, result.Id);
    EXPECT_EQ(call.Instigator, player);
}

TEST_F(GoldenAuthoredApiTest, AVerbWithoutArgumentsAndOneWithAStringBothRun)
{
    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    EXPECT_EQ(Invoke(Binding("test.torch.extinguish_all", {})).Status, VerbAdmission::Accepted);
    EXPECT_EQ(Log().Extinguishes, 1);

    const EntityId torch = Entities.CreateEntity();
    const AuthoredValue input = AuthoredValue::Entity(torch);
    EXPECT_EQ(Invoke(Binding("test.torch.rename", { FromInput("torch", "which") }, { "which" }),
                     { &input, 1 })
                  .Status,
              VerbAdmission::Accepted);
    ASSERT_EQ(Log().Names.size(), 1u);
    EXPECT_EQ(Log().Names.front(), "torch");
}

TEST_F(GoldenAuthoredApiTest, AnInputOfTheWrongKindIsRefusedBeforeTheMethodRuns)
{
    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    const AuthoredValue wrong = AuthoredValue::Int(3);
    const VerbInvocationResult result = Invoke(
        Binding("test.torch.ignite", { FromInput("torch", "which") }, { "which" }), { &wrong, 1 });
    EXPECT_EQ(result.Status, VerbAdmission::InvalidArguments);
    EXPECT_TRUE(Log().Ignites.empty());
}

TEST_F(GoldenAuthoredApiTest, ResettingTheBindingsTakesTheObjectAwayFromEveryEntry)
{
    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    bindings.Reset();
    EXPECT_EQ(Invoke(Binding("test.torch.extinguish_all", {})).Status, VerbAdmission::Unavailable);
    const AuthoredQueryId query = Queries->Registry().Find("test.torch.can_ignite");
    AuthoredValue result;
    const std::vector<AuthoredValue> arguments{ AuthoredValue::Entity(Entities.CreateEntity()),
                                                AuthoredValue::Int(5) };
    EXPECT_EQ(Queries->Evaluate(query, arguments, result), AuthoredQueryStatus::Unbound);
}

TEST_F(GoldenAuthoredApiTest, AComputedQueryAnswersThroughTheConstMethod)
{
    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    const AuthoredQueryId query = Queries->Registry().Find("test.torch.can_ignite");
    const EntityId torch = Entities.CreateEntity();

    AuthoredValue result;
    std::vector<AuthoredValue> arguments{ AuthoredValue::Entity(torch), AuthoredValue::Int(5) };
    ASSERT_EQ(Queries->Evaluate(query, arguments, result), AuthoredQueryStatus::Value);
    bool answer = false;
    ASSERT_TRUE(result.TryGetBool(answer));
    EXPECT_TRUE(answer);

    // Outside the declared range: refused by the declaration, not answered.
    arguments[1] = AuthoredValue::Int(11);
    EXPECT_EQ(Queries->Evaluate(query, arguments, result), AuthoredQueryStatus::InvalidArguments);
}

TEST_F(GoldenAuthoredApiTest, AnEmptyOptionalAnswerIsUnavailableNotAValue)
{
    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    const AuthoredQueryId query = Queries->Registry().Find("test.torch.burn_time");
    const AuthoredQueryDefinition& definition = *Queries->Registry().Get(query);
    // The declared result is what a Value holds, never an optional.
    EXPECT_EQ(definition.Result.Kind, DataFieldKind::Float);

    AuthoredValue result;
    EntityId torch;
    torch.Index = 7;
    const std::vector<AuthoredValue> seven{ AuthoredValue::Entity(torch) };
    EXPECT_EQ(Queries->Evaluate(query, seven, result), AuthoredQueryStatus::Unavailable);
    EXPECT_TRUE(result.IsNone());

    torch.Index = 8;
    const std::vector<AuthoredValue> eight{ AuthoredValue::Entity(torch) };
    ASSERT_EQ(Queries->Evaluate(query, eight, result), AuthoredQueryStatus::Value);
    double seconds = 0.0;
    ASSERT_TRUE(result.TryGetFloat(seconds));
    EXPECT_DOUBLE_EQ(seconds, 12.5);
}

TEST_F(GoldenAuthoredApiTest, AMemberQueryReadsTheRowAndIsUnavailableWithoutOne)
{
    AuthoredApiBindings bindings = BindAuthoredApi<GoldenTorch>(*Queries, std::as_const(Entities));
    EXPECT_TRUE(bindings.Unbound().empty());

    const AuthoredQueryId lit = Queries->Registry().Find("test.codegen.golden_torch.lit");
    const AuthoredQueryId hot = Queries->Registry().Find("test.codegen.golden_torch.hot");
    ASSERT_TRUE(lit.IsValid());
    ASSERT_TRUE(hot.IsValid());
    // The implicit argument names the component it reads.
    EXPECT_EQ(Queries->Registry().Get(lit)->Arguments.Children.front().Reference.ComponentIdentity,
              "test.codegen.golden_torch");

    const EntityId torch = Entities.CreateEntity();
    Entities.AddComponent<GoldenTorch>(torch, GoldenTorch{ .Lit = true, .Hot = true });
    const EntityId bare = Entities.CreateEntity();

    AuthoredValue result;
    const std::vector<AuthoredValue> onTorch{ AuthoredValue::Entity(torch) };
    ASSERT_EQ(Queries->Evaluate(lit, onTorch, result), AuthoredQueryStatus::Value);
    bool value = false;
    ASSERT_TRUE(result.TryGetBool(value));
    EXPECT_TRUE(value);
    ASSERT_EQ(Queries->Evaluate(hot, onTorch, result), AuthoredQueryStatus::Value);

    // No torch here: the question does not apply, which is not "false".
    const std::vector<AuthoredValue> onBare{ AuthoredValue::Entity(bare) };
    EXPECT_EQ(Queries->Evaluate(lit, onBare, result), AuthoredQueryStatus::Unavailable);
}

TEST_F(GoldenAuthoredApiTest, AnEventDeclaresItsPayloadAndPublishesItEncoded)
{
    const AuthoredEventRegistry& events = *FindAuthoredEventRegistry(Entities);
    const AuthoredEventId lit = events.Find("test.torch.lit");
    ASSERT_TRUE(lit.IsValid());
    const AuthoredEventDefinition& definition = *events.Get(lit);
    EXPECT_EQ(definition.SourceComponent, "test.codegen.golden_torch");
    ASSERT_EQ(definition.Payload.Children.size(), 2u);
    EXPECT_EQ(definition.Payload.Children[0].Key, "instigator");
    EXPECT_EQ(definition.Payload.Children[1].Kind, DataFieldKind::Enum);

    LoggingProvider logging;
    AuthoredEventDispatcher dispatcher(events, logging.GetLogger<GoldenAuthoredApiTest>());
    HeardBrightness heard;
    AuthoredEventSubscription subscription =
        dispatcher.Subscribe<&HeardBrightness::Deliver>(lit, EntityId{}, heard);

    const EntityId torch = Entities.CreateEntity();
    EXPECT_TRUE(dispatcher.Publish(torch, GoldenTorchLitEvent{ .Brightness = GoldenLamp::Dim }));
    EXPECT_TRUE(dispatcher.Publish(torch, GoldenTorchExtinguishedEvent{}));
    (void)dispatcher.Drain(1);
    ASSERT_EQ(heard.Brightness.size(), 1u);
    EXPECT_EQ(heard.Brightness.front(), "dim");
}
