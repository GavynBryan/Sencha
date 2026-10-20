#include <authored/AuthoredArgumentBinding.h>
#include <core/json/JsonParser.h>
#include <gtest/gtest.h>
#include <logic/TriggerProgram.h>
#include <world/identity/PersistentEntityIndex.h>

namespace
{
class TriggerProgramTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        VerbRegistrationScope verbs(Verbs, "test");
        VerbDefinition call;
        call.Name = "test.call";
        call.Arguments.Children = {MakeDataField(DataFieldKind::Entity, "target", "Target"),
                                   MakeDataField(DataFieldKind::Float, "amount", "Amount")};
        call.Arguments.Children[1].Default = 3.0;
        (void)verbs.Declare(std::move(call));
        ASSERT_TRUE(verbs.Commit());
        AuthoredQueryRegistrationScope queries(Queries, "test");
        AuthoredQueryDefinition query;
        query.Name = "test.query";
        query.Arguments.Children = {MakeDataField(DataFieldKind::Entity, "target", "Target"),
                                    MakeDataField(DataFieldKind::Entity, "other", "Other")};
        query.Result = MakeDataField(DataFieldKind::Bool, "result", "Result");
        (void)queries.Declare(std::move(query));
        ASSERT_TRUE(queries.Commit());
    }
    bool Compile(std::string_view text)
    {
        Errors.clear();
        const auto parsed = JsonParse(text);
        EXPECT_TRUE(parsed.has_value());
        return parsed.has_value() && CompileTriggerProgram(*parsed, {.Verbs = &Verbs}, &Queries, Compiled, Errors);
    }
    VerbRegistry Verbs;
    AuthoredQueryRegistry Queries;
    CompiledTriggerProgram Compiled;
    std::vector<std::string> Errors;
};
} // namespace
TEST_F(TriggerProgramTest, VerbsAndQueriesShareDefaultsConstantsAndInputs)
{
    ASSERT_TRUE(Compile(R"({"version":1,"commands":[{"verb":"test.call","arguments":{"target":{"input":"activator"}}}],
        "conditions":[{"query":"test.query","arguments":{"target":{"input":"activator"},"other":{"input":"trigger"}}}]})"));
    ASSERT_EQ(Compiled.Commands.size(), 1u);
    double amount = 0;
    EXPECT_TRUE(Compiled.Commands[0].Binding.Constants.TryGetFloat(1, amount));
    EXPECT_EQ(amount, 3);
    ASSERT_EQ(Compiled.Conditions.size(), 1u);
    EXPECT_EQ(Compiled.Conditions[0].Inputs.size(), 2u);
}
TEST_F(TriggerProgramTest, InvalidCallsHaveLocatedErrors)
{
    for (const auto text :
         {R"({"version":1,"commands":[{"verb":"test.call"}]})",
          R"({"version":1,"commands":[{"verb":"test.call","arguments":{"target":{"const":42}}}]})",
          R"({"version":1,"commands":[{"verb":"test.call","arguments":{"target":{"input":"trigger_up"}}}]})",
          R"({"version":1,"commands":[{"verb":"test.call","arguments":{"target":{"input":"unknown_context"}}}]})",
          R"({"version":1,"commands":[{"verb":"missing.call"}]})",
          R"({"version":1,"conditions":[{"query":"missing.query"}]})"})
    {
        EXPECT_FALSE(Compile(text));
        ASSERT_FALSE(Errors.empty());
        EXPECT_NE(Errors[0].find("[0]"), std::string::npos);
    }
}
TEST_F(TriggerProgramTest, PersistentQueryEntitiesResolveAtInvocationTime)
{
    ASSERT_TRUE(Compile(R"({"version":1,"conditions":[{"query":"test.query","arguments":{
        "target":{"input":"activator"},"other":{"entity":"00000000000000ab"}}}]})"));
    const auto &binding = Compiled.Conditions[0].Binding;
    PersistentEntityIndex index;
    const EntityId target{7, 42}, other{9, 13};
    ASSERT_TRUE(index.Register(PersistentEntityId{0xab}, other));
    const AuthoredValue input = AuthoredValue::Entity(target);
    AuthoredArguments arguments;
    ASSERT_EQ(ResolveAuthoredArguments(binding.Constants, binding.Inputs, {&input, 1}, &index, nullptr, arguments),
              VerbAdmission::Accepted);
    EntityId actual;
    EXPECT_TRUE(arguments.TryGetEntity(0, actual));
    EXPECT_EQ(actual, target);
    EXPECT_TRUE(arguments.TryGetEntity(1, actual));
    EXPECT_EQ(actual, other);
    index.Unregister(PersistentEntityId{0xab}, other);
    EXPECT_EQ(ResolveAuthoredArguments(binding.Constants, binding.Inputs, {&input, 1}, &index, nullptr, arguments),
              VerbAdmission::UnresolvedReference);
}
TEST_F(TriggerProgramTest, LeasesNeedAnExplicitLifetimeOwner)
{
    VerbDefinition lease;
    lease.Name = "test.held";
    lease.Kind = VerbKind::Lease;
    VerbRegistrationScope scope(Verbs, "test");
    (void)scope.Declare(std::move(lease));
    ASSERT_TRUE(scope.Commit());
    EXPECT_FALSE(Compile(R"({"version":1,"commands":[{"verb":"test.held"}]})"));
    ASSERT_FALSE(Errors.empty());
    EXPECT_NE(Errors[0].find("lease"), std::string::npos);
}
TEST_F(TriggerProgramTest, ProgramsHaveExplicitVersionAndSizeBounds)
{
    EXPECT_FALSE(Compile(R"({"version":2})"));
    EXPECT_FALSE(Compile(R"({"commands":[]})"));
    auto source = EmptyTriggerProgram();
    source.Find("commands")->AsArray().resize(kTriggerCommandLimit + 1, JsonValue::Object{});
    EXPECT_FALSE(CompileTriggerProgram(source, {.Verbs = &Verbs}, &Queries, Compiled, Errors));
    EXPECT_TRUE(CompileTriggerProgram(EmptyTriggerProgram(), {.Verbs = &Verbs}, &Queries, Compiled, Errors));
}
TEST_F(TriggerProgramTest, ActivationIntervalsMustBeFiniteAndValid)
{
    Trigger trigger;
    EXPECT_TRUE(ValidateTriggerActivation(trigger, Errors));
    trigger.StayInterval = 0;
    EXPECT_FALSE(ValidateTriggerActivation(trigger, Errors));
    trigger.StayInterval = 0.1f;
    trigger.Cooldown = -1;
    EXPECT_FALSE(ValidateTriggerActivation(trigger, Errors));
}

TEST_F(TriggerProgramTest, DependenciesAreAvailableBeforeVocabularyOrSubtypeCompilation)
{
    const auto source = JsonParse(R"({"version":1,"commands":[{"verb":"game.unavailable","arguments":{
        "definition":{"data":"asset://data/definition.sdata"},"effect":{"asset":"asset://effects/test.sdata"}}}],
        "conditions":[{"query":"game.unavailable","arguments":{"rule":{"data":"asset://data/rule.sdata"}}}]})");
    ASSERT_TRUE(source);
    std::vector<AssetRef> dependencies;
    ASSERT_TRUE(CollectTriggerProgramDependencies(*source, dependencies, Errors));
    ASSERT_EQ(dependencies.size(), 3u);
    EXPECT_EQ(dependencies[0].Type, AssetType::Data);
    EXPECT_EQ(dependencies[0].Path, "asset://data/definition.sdata");
    EXPECT_EQ(dependencies[1].Type, AssetType::Unknown);
    EXPECT_EQ(dependencies[2].Path, "asset://data/rule.sdata");
}

TEST_F(TriggerProgramTest, DependencyCollectionSkipsDisabledUnknownCalls)
{
    const auto source = JsonParse(R"({"version":1,"commands":[{"enabled":false,"verb":"game.unknown",
        "arguments":{"broken":{"new_source":{"asset":"asset://missing.sdata"}}}}]})");
    ASSERT_TRUE(source);
    std::vector<AssetRef> dependencies;
    EXPECT_TRUE(CollectTriggerProgramDependencies(*source, dependencies, Errors));
    EXPECT_TRUE(dependencies.empty());
    EXPECT_TRUE(Errors.empty());
}

TEST_F(TriggerProgramTest, InvalidDependencyArgumentsKeepTheirRowAndArgumentLocation)
{
    const auto source = JsonParse(R"({"version":1,"conditions":[{"query":"game.rule",
        "arguments":{"rule":{"data":17}}}]})");
    ASSERT_TRUE(source);
    std::vector<AssetRef> dependencies;
    EXPECT_FALSE(CollectTriggerProgramDependencies(*source, dependencies, Errors));
    ASSERT_FALSE(Errors.empty());
    EXPECT_NE(Errors[0].find("conditions[0]"), std::string::npos);
    EXPECT_NE(Errors[0].find("rule"), std::string::npos);
}
