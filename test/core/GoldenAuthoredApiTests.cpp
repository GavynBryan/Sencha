// Compiles and runs the golden generated companion; check_component_codegen.sh
// byte-compares it against a real generator run.

#include "component_codegen/GoldenAuthoredApi.h"
#include "component_codegen/GoldenAuthoredApi.sencha.h.expected"

#include <authored/AuthoredApi.h>
#include <authored/AuthoredEventDispatcher.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/WorldVocabulary.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

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
    std::vector<float> Holds;
    std::vector<LeaseOwner> Ended;
    std::vector<std::string> Glints;
    int Fuelled = 0;
    bool HoldGoesOn = true;
    GoldenRefusal Douses = GoldenRefusal::None;
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

VerbResult<EntityId, GoldenRefusal> GoldenTorchSystem::Light(EntityId torch, float fuel)
{
    if (fuel == 0.0f)
        return GoldenRefusal::Spent;
    return torch;
}

VerbRefusal<GoldenRefusal> GoldenTorchSystem::Douse(std::optional<EntityId>)
{
    return Log().Douses;
}

VerbAnswer GoldenTorchSystem::Hold(const VerbInvocation&, EntityId, float seconds)
{
    Log().Holds.push_back(seconds);
    return VerbAnswer{};
}

VerbAnswer GoldenTorchSystem::EndHold(const VerbInvocation& invocation)
{
    Log().Ended.push_back(invocation.Owner);
    return VerbAnswer{};
}

bool GoldenTorchSystem::HoldGoesOn(const LeaseOwner&) const
{
    return Log().HoldGoesOn;
}

VerbAdmission GoldenTorchSystem::Flicker(std::int32_t)
{
    return VerbAdmission::Accepted;
}

VerbAdmission GoldenTorchSystem::EndFlicker(const VerbInvocation&)
{
    return VerbAdmission::Accepted;
}

VerbAdmission GoldenTorchSystem::Glint(const AssetRef& mesh, std::string_view note)
{
    Log().Glints.push_back(mesh.Path + ":" + std::string(note));
    return VerbAdmission::Accepted;
}

VerbAdmission GoldenTorchSystem::FuelWith(AuthoredDataRef fuel)
{
    Log().Fuelled += fuel.Reference != nullptr ? 1 : 0;
    return VerbAdmission::Accepted;
}

std::optional<GoldenFlame> GoldenTorchSystem::Flame(EntityId torch) const
{
    if (torch.Index == 7)
        return std::nullopt;
    return GoldenFlame{ .Height = 0.5f, .Lamp = GoldenLamp::Dim };
}

std::vector<EntityId> GoldenTorchSystem::LitTorches() const
{
    return { EntityId{ .Index = 2, .Generation = 1 }, EntityId{ .Index = 3, .Generation = 1 } };
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
        Torches.Entities = &Entities;
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
                                              EntityId instigator = {},
                                              AuthoredValue* result = nullptr)
    {
        std::vector<std::string> errors;
        CompiledVerbBinding compiled;
        EXPECT_TRUE(CompileVerbBinding(desc, MakeVerbBindingEnvironment(Entities), compiled, errors))
            << (errors.empty() ? std::string{} : errors.front());
        VerbInvocationSource source;
        source.Instigator = instigator;
        return Verbs->Invoke(compiled, inputs, source, result);
    }

    [[nodiscard]] CompiledVerbBinding Compiled(const VerbBindingDesc& desc)
    {
        std::vector<std::string> errors;
        CompiledVerbBinding compiled;
        EXPECT_TRUE(CompileVerbBinding(desc, MakeVerbBindingEnvironment(Entities), compiled, errors))
            << (errors.empty() ? std::string{} : errors.front());
        return compiled;
    }

    [[nodiscard]] const VerbDefinition& Verb(std::string_view name) const
    {
        const VerbRegistry& verbs = *FindVerbRegistry(Entities);
        return *verbs.Get(verbs.Find(name));
    }

    World Entities;
    std::optional<VerbDispatcher> Verbs;
    std::optional<AuthoredQueryDispatcher> Queries;
    GoldenTorchSystem Torches;
};
} // namespace

TEST_F(GoldenAuthoredApiTest, OnlyAComponentCanBeATargetOrASource)
{
    static_assert(AuthoredComponentType<GoldenTorch>);
    static_assert(!AuthoredComponentType<GoldenTorchSystem>);
    static_assert(!AuthoredComponentType<GoldenLamp>);
    SUCCEED();
}

TEST_F(GoldenAuthoredApiTest, DeclaresTheContractTheSignatureStates)
{
    const VerbRegistry& verbs = *FindVerbRegistry(Entities);
    const VerbDefinition* ignite = verbs.Get(verbs.Find("test.torch.ignite"));
    ASSERT_NE(ignite, nullptr);
    EXPECT_EQ(ignite->DisplayName, "Ignite");
    EXPECT_EQ(ignite->Category, "Test");

    const std::vector<DataFieldSchema>& arguments = ignite->Arguments.Children;
    ASSERT_EQ(arguments.size(), 4u);

    EXPECT_EQ(arguments[0].Key, "torch");
    EXPECT_EQ(arguments[0].Kind, DataFieldKind::Entity);
    EXPECT_EQ(arguments[0].Reference.ComponentIdentity, "test.codegen.golden_torch");
    EXPECT_TRUE(arguments[0].Required);

    EXPECT_EQ(arguments[1].Kind, DataFieldKind::Enum);
    ASSERT_EQ(arguments[1].EnumChoices.size(), 3u);
    EXPECT_EQ(arguments[1].EnumChoices[2].Value, "bright");
    EXPECT_EQ(arguments[1].Default, DataDefaultValue(std::string("bright")));
    EXPECT_FALSE(arguments[1].Required);

    EXPECT_EQ(arguments[2].Kind, DataFieldKind::Float);
    EXPECT_EQ(arguments[2].Numeric.Minimum, 0.5);
    EXPECT_EQ(arguments[2].Numeric.Maximum, 60.0);
    EXPECT_EQ(arguments[2].Default, DataDefaultValue(5.0));

    EXPECT_EQ(arguments[3].Kind, DataFieldKind::Optional);
    EXPECT_FALSE(arguments[3].Required);
    ASSERT_EQ(arguments[3].Children.size(), 1u);
    EXPECT_EQ(arguments[3].Children[0].Kind, DataFieldKind::Entity);
}

TEST_F(GoldenAuthoredApiTest, AVerbReachesTheMethodWithEveryArgumentDecoded)
{
    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    EXPECT_TRUE(bindings.Unbound().empty());
    EXPECT_EQ(bindings.BoundCount(), 13u);

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
    EXPECT_FLOAT_EQ(call.Seconds, 5.0f);
    EXPECT_FALSE(call.Named.has_value());
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
    const AuthoredQueryHandle query = Queries->Registry().Resolve("test.torch.can_ignite");
    AuthoredValue result;
    const std::vector<AuthoredValue> arguments{ AuthoredValue::Entity(Entities.CreateEntity()),
                                                AuthoredValue::Int(5) };
    EXPECT_EQ(Queries->Evaluate(query, arguments, result), AuthoredQueryStatus::Unbound);
}

TEST_F(GoldenAuthoredApiTest, AComputedQueryAnswersThroughTheConstMethod)
{
    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    const AuthoredQueryHandle query = Queries->Registry().Resolve("test.torch.can_ignite");
    const EntityId torch = Entities.CreateEntity();

    AuthoredValue result;
    std::vector<AuthoredValue> arguments{ AuthoredValue::Entity(torch), AuthoredValue::Int(5) };
    ASSERT_EQ(Queries->Evaluate(query, arguments, result), AuthoredQueryStatus::Value);
    bool answer = false;
    ASSERT_TRUE(result.TryGetBool(answer));
    EXPECT_TRUE(answer);

    arguments[1] = AuthoredValue::Int(11);
    EXPECT_EQ(Queries->Evaluate(query, arguments, result), AuthoredQueryStatus::InvalidArguments);
}

TEST_F(GoldenAuthoredApiTest, AnEmptyOptionalAnswerIsUnavailableNotAValue)
{
    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    const AuthoredQueryHandle query = Queries->Registry().Resolve("test.torch.burn_time");
    const AuthoredQueryDefinition& definition = *Queries->Registry().Get(query.Slot);
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

    const AuthoredQueryHandle lit = Queries->Registry().Resolve("test.codegen.golden_torch.lit");
    const AuthoredQueryHandle hot = Queries->Registry().Resolve("test.codegen.golden_torch.hot");
    ASSERT_TRUE(lit.IsValid());
    ASSERT_TRUE(hot.IsValid());
    EXPECT_EQ(Queries->Registry().Get(lit.Slot)->Arguments.Children.front().Reference.ComponentIdentity,
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
        dispatcher.Subscribe<&HeardBrightness::Deliver>(events.Resolve("test.torch.lit"), EntityId{}, heard);

    const EntityId torch = Entities.CreateEntity();
    EXPECT_TRUE(dispatcher.Publish(torch, GoldenTorchLitEvent{ .Brightness = GoldenLamp::Dim }));
    EXPECT_TRUE(dispatcher.Publish(torch, GoldenTorchExtinguishedEvent{}));
    (void)dispatcher.Drain(1);
    ASSERT_EQ(heard.Brightness.size(), 1u);
    EXPECT_EQ(heard.Brightness.front(), "dim");
}

TEST_F(GoldenAuthoredApiTest, ADeclarationNamesItselfForCodeThatMustNameIt)
{
    static_assert(AuthoredApiDefinition<GoldenTorchSystem>::Name_Light == "test.torch.light");
    static_assert(AuthoredApiDefinition<GoldenTorchSystem>::Name_LitTorches == "test.torch.lit");
    EXPECT_EQ(Verb(AuthoredApiDefinition<GoldenTorchSystem>::Name_Hold).Name, "test.torch.hold");
}

TEST_F(GoldenAuthoredApiTest, ALeaseIsHeldReleasedAndAskedWhetherItsHoldGoesOn)
{
    const VerbDefinition& hold = Verb("test.torch.hold");
    EXPECT_EQ(hold.Kind, VerbKind::Lease);
    EXPECT_TRUE(hold.Completes);
    EXPECT_EQ(hold.Prediction, AuthoredPrediction::Simulation);
    EXPECT_EQ(hold.Scope, AuthoredScope::Gameplay);

    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    const EntityId torch = Entities.CreateEntity();
    const LeaseOwner owner{ .Entity = Entities.CreateEntity(), .Key = 4 };
    const AuthoredValue input = AuthoredValue::Entity(torch);
    const CompiledVerbBinding binding =
        Compiled(Binding("test.torch.hold", { FromInput("torch", "which"), Literal("seconds", 2.5) }, { "which" }));

    ASSERT_TRUE(Verbs->AcquireLease(binding, { &input, 1 }, owner).Accepted());
    ASSERT_EQ(Log().Holds.size(), 1u);
    EXPECT_FLOAT_EQ(Log().Holds.front(), 2.5f);
    EXPECT_TRUE(Verbs->LeaseHolds(binding.Verb, owner));
    Log().HoldGoesOn = false;
    EXPECT_FALSE(Verbs->LeaseHolds(binding.Verb, owner));
    EXPECT_EQ(Verbs->ReleaseLease(binding.Verb, owner), VerbAdmission::Accepted);
    ASSERT_EQ(Log().Ended.size(), 1u);
    EXPECT_EQ(Log().Ended.front(), owner);
}

TEST_F(GoldenAuthoredApiTest, ACosmeticVerbAndACosmeticLeaseArePredictedAsPresentation)
{
    for (const std::string_view name : { "test.torch.glint", "test.torch.flicker" })
    {
        const VerbDefinition& cosmetic = Verb(name);
        EXPECT_EQ(cosmetic.Scope, AuthoredScope::Cosmetic) << name;
        EXPECT_EQ(cosmetic.Prediction, AuthoredPrediction::Presentation) << name;
        EXPECT_FALSE(cosmetic.Completes) << name;
    }
    EXPECT_EQ(Verb("test.torch.flicker").Kind, VerbKind::Lease);
    EXPECT_EQ(Verb("test.torch.glint").Kind, VerbKind::Instant);
}

TEST_F(GoldenAuthoredApiTest, AResultIsDeclaredAndAnsweredOnlyWhenAccepted)
{
    const VerbDefinition& light = Verb("test.torch.light");
    ASSERT_TRUE(light.Result.has_value());
    EXPECT_EQ(light.Result->Key, "flame");
    EXPECT_EQ(light.Result->Kind, DataFieldKind::Entity);
    EXPECT_EQ(light.Result->Reference.ComponentIdentity, "test.codegen.golden_torch");
    ASSERT_EQ(light.Refusals.size(), 2u);
    EXPECT_EQ(light.Refusals[0].Name, "wet");
    EXPECT_EQ(light.Refusals[1].Name, "spent");

    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    const EntityId torch = Entities.CreateEntity();
    const AuthoredValue input = AuthoredValue::Entity(torch);
    AuthoredValue answered;
    ASSERT_TRUE(Invoke(Binding("test.torch.light", { FromInput("torch", "which") }, { "which" }), { &input, 1 }, {},
                       &answered)
                    .Accepted());
    EntityId flame;
    ASSERT_TRUE(answered.TryGetEntity(flame));
    EXPECT_EQ(flame, torch);

    const VerbInvocationResult spent =
        Invoke(Binding("test.torch.light", { FromInput("torch", "which"), Literal("fuel", 0.0) }, { "which" }),
               { &input, 1 }, {}, &answered);
    EXPECT_EQ(spent.Status, VerbAdmission::Refused);
    EXPECT_EQ(spent.Detail, static_cast<std::uint16_t>(GoldenRefusal::Spent));
    EXPECT_TRUE(answered.IsNone());
}

TEST_F(GoldenAuthoredApiTest, ARefusalNamesItsReason)
{
    const VerbDefinition& douse = Verb("test.torch.douse");
    EXPECT_FALSE(douse.Result.has_value());
    ASSERT_EQ(douse.Refusals.size(), 2u);
    EXPECT_EQ(douse.Prediction, AuthoredPrediction::Simulation);

    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    Log().Douses = GoldenRefusal::Wet;
    const VerbInvocationResult refused = Invoke(Binding("test.torch.douse", {}));
    EXPECT_EQ(refused.Status, VerbAdmission::Refused);
    EXPECT_EQ(DescribeVerbAnswer(*FindVerbRegistry(Entities), FindVerbRegistry(Entities)->Find("test.torch.douse"),
                                 AnswerOf(refused)),
              "refused: wet");
    Log().Douses = GoldenRefusal::None;
    EXPECT_TRUE(Invoke(Binding("test.torch.douse", {})).Accepted()) << "an absent optional target is not checked";
}

TEST_F(GoldenAuthoredApiTest, ATargetThatIsNotAliveIsAnUnresolvedReferenceNamingIt)
{
    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    const EntityId torch = Entities.CreateEntity();
    Entities.DestroyEntity(torch);
    const AuthoredValue input = AuthoredValue::Entity(torch);

    const VerbInvocationResult result =
        Invoke(Binding("test.torch.ignite", { FromInput("torch", "which") }, { "which" }), { &input, 1 });
    EXPECT_EQ(result.Status, VerbAdmission::UnresolvedReference);
    EXPECT_EQ(DescribeVerbAnswer(*FindVerbRegistry(Entities), FindVerbRegistry(Entities)->Find("test.torch.ignite"),
                                 AnswerOf(result)),
              "unresolved reference 'torch'");
    EXPECT_TRUE(Log().Ignites.empty());

    const VerbInvocationResult present =
        Invoke(Binding("test.torch.douse", { FromInput("torch", "which") }, { "which" }), { &input, 1 });
    EXPECT_EQ(present.Status, VerbAdmission::UnresolvedReference) << "a present optional target is checked";
}

TEST_F(GoldenAuthoredApiTest, BoundsDescriptionsUnitsAndReferencesAreDeclaredOnTheArgument)
{
    const DataFieldSchema& fuel = Verb("test.torch.light").Arguments.Children[1];
    EXPECT_EQ(fuel.Summary, "How long it burns.");
    EXPECT_EQ(fuel.Units, "seconds");
    EXPECT_EQ(fuel.Numeric.Minimum, 0.0);
    EXPECT_FALSE(fuel.Numeric.Maximum.has_value());

    const DataFieldSchema& seconds = Verb("test.torch.hold").Arguments.Children[1];
    EXPECT_EQ(seconds.Numeric.Minimum, 0.0);
    EXPECT_EQ(seconds.Numeric.Maximum, 30.0);

    const DataFieldSchema& rate = Verb("test.torch.flicker").Arguments.Children[0];
    EXPECT_EQ(rate.Numeric.Maximum, 8.0);
    EXPECT_EQ(rate.Numeric.Minimum, static_cast<double>(std::numeric_limits<std::int32_t>::min()));

    const DataFieldSchema& mesh = Verb("test.torch.glint").Arguments.Children[0];
    EXPECT_EQ(mesh.Kind, DataFieldKind::AssetRef);
    EXPECT_EQ(mesh.Reference.AssetTypeFilter, AssetType::StaticMesh);
    const DataFieldSchema& note = Verb("test.torch.glint").Arguments.Children[1];
    EXPECT_EQ(note.Kind, DataFieldKind::String);
    EXPECT_EQ(note.Default, DataDefaultValue(std::string("glint")));

    const DataFieldSchema& data = Verb("test.torch.fuel_with").Arguments.Children[0];
    EXPECT_EQ(data.Kind, DataFieldKind::DataAssetRef);
    EXPECT_EQ(data.Reference.DataSubtype, "test.fuel");
}

TEST_F(GoldenAuthoredApiTest, AStringAndAnAssetAreReadInPlace)
{
    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    VerbBindingArgument mesh;
    mesh.Key = "mesh";
    mesh.Source = VerbArgumentSource::Asset;
    mesh.Text = "asset://meshes/torch.smesh";
    ASSERT_TRUE(Invoke(Binding("test.torch.glint", { mesh, Literal("note", "bright") })).Accepted());
    ASSERT_EQ(Log().Glints.size(), 1u);
    EXPECT_EQ(Log().Glints.front(), "asset://meshes/torch.smesh:bright");
}

TEST_F(GoldenAuthoredApiTest, ARecordAndAnArrayAnswerWhole)
{
    AuthoredApiBindings bindings = BindAuthoredApi(&*Verbs, &*Queries, Torches);
    const AuthoredQueryHandle flame = Queries->Registry().Resolve("test.torch.flame");
    const AuthoredQueryDefinition& record = *Queries->Registry().Get(flame.Slot);
    ASSERT_EQ(record.Result.Kind, DataFieldKind::Record);
    ASSERT_EQ(record.Result.Children.size(), 2u);
    EXPECT_EQ(record.Result.Children[0].Key, "height");
    EXPECT_EQ(record.Result.Children[1].DisplayName, "Lamp");

    AuthoredValue result;
    const std::vector<AuthoredValue> arguments{ AuthoredValue::Entity(Entities.CreateEntity()) };
    ASSERT_EQ(Queries->Evaluate(flame, arguments, result), AuthoredQueryStatus::Value);
    ASSERT_EQ(result.Kind(), AuthoredValueKind::Record);
    ASSERT_EQ(result.Children().size(), 2u);
    double height = 0.0;
    ASSERT_TRUE(result.Children()[0].TryGetFloat(height));
    EXPECT_DOUBLE_EQ(height, 0.5);

    const AuthoredQueryHandle lit = Queries->Registry().Resolve("test.torch.lit");
    const AuthoredQueryDefinition& array = *Queries->Registry().Get(lit.Slot);
    ASSERT_EQ(array.Result.Kind, DataFieldKind::Array);
    ASSERT_EQ(array.Result.Children.size(), 1u);
    EXPECT_EQ(array.Result.Children[0].Kind, DataFieldKind::Entity);
    ASSERT_EQ(Queries->Evaluate(lit, {}, result), AuthoredQueryStatus::Value);
    ASSERT_EQ(result.Kind(), AuthoredValueKind::Array);
    EXPECT_EQ(result.Children().size(), 2u);
}
