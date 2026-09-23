#include "AllocationCounter.h"

#include <authored/AuthoredQueryDispatcher.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <optional>
#include <string>
#include <vector>

namespace
{
[[nodiscard]] AuthoredQueryDefinition Question(std::string name, std::int64_t maximum = 100)
{
    AuthoredQueryDefinition definition;
    definition.Name = std::move(name);
    DataFieldSchema level;
    level.Key = "level";
    level.Kind = DataFieldKind::Int;
    level.Numeric.Maximum = static_cast<double>(maximum);
    definition.Arguments.Children.push_back(std::move(level));
    definition.Result.Kind = DataFieldKind::Int;
    return definition;
}

bool Declare(AuthoredQueryRegistry& registry, AuthoredQueryDefinition definition)
{
    AuthoredQueryRegistrationScope scope(registry, "test");
    (void)scope.Declare(std::move(definition));
    return scope.Commit();
}

struct Doubler
{
    mutable std::int64_t Calls = 0;

    static AuthoredQueryStatus Evaluate(const Doubler& self,
                                        std::span<const AuthoredValue> arguments,
                                        AuthoredValue& result)
    {
        ++self.Calls;
        std::int64_t level = 0;
        if (!arguments[0].TryGetInt(level))
            return AuthoredQueryStatus::InvalidArguments;
        if (level < 0)
            return AuthoredQueryStatus::Unavailable;
        result = AuthoredValue::Int(level * 2);
        return AuthoredQueryStatus::Value;
    }
};

class AuthoredQueryDispatchTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(Declare(Registry, Question("test.double")));
        Id = Registry.Find("test.double");
        Handle = Registry.Resolve("test.double");
        Dispatcher.emplace(Registry);
    }

    [[nodiscard]] AuthoredQueryStatus Ask(std::int64_t level, AuthoredValue& result) const
    {
        return AskWith(Handle, level, result);
    }

    [[nodiscard]] AuthoredQueryStatus AskWith(const AuthoredQueryHandle& handle,
                                              std::int64_t level,
                                              AuthoredValue& result) const
    {
        const AuthoredValue argument = AuthoredValue::Int(level);
        return Dispatcher->Evaluate(handle, { &argument, 1 }, result);
    }

    AuthoredQueryRegistry Registry;
    AuthoredQueryId Id;
    AuthoredQueryHandle Handle;
    std::optional<AuthoredQueryDispatcher> Dispatcher;
    Doubler Answerer;
};
} // namespace

TEST_F(AuthoredQueryDispatchTest, ABoundQueryAnswersWithAValue)
{
    AuthoredQueryBindingToken token = Dispatcher->Bind<&Doubler::Evaluate>(Id, Answerer);
    ASSERT_TRUE(token.IsValid());

    AuthoredValue result;
    ASSERT_EQ(Ask(21, result), AuthoredQueryStatus::Value);
    std::int64_t answer = 0;
    ASSERT_TRUE(result.TryGetInt(answer));
    EXPECT_EQ(answer, 42);
}

TEST_F(AuthoredQueryDispatchTest, EachRefusalHasItsOwnStatusAndWritesNothing)
{
    AuthoredValue result = AuthoredValue::Bool(true);
    EXPECT_EQ(Ask(1, result), AuthoredQueryStatus::Unbound);

    AuthoredQueryBindingToken token = Dispatcher->Bind<&Doubler::Evaluate>(Id, Answerer);
    EXPECT_EQ(Ask(101, result), AuthoredQueryStatus::InvalidArguments);
    EXPECT_EQ(Answerer.Calls, 0);
    EXPECT_EQ(Dispatcher->Evaluate(Handle, {}, result), AuthoredQueryStatus::InvalidArguments);
    EXPECT_EQ(Ask(-1, result), AuthoredQueryStatus::Unavailable);
    EXPECT_EQ(Dispatcher->Evaluate(AuthoredQueryHandle{}, {}, result), AuthoredQueryStatus::Stale);

    bool untouched = false;
    EXPECT_TRUE(result.TryGetBool(untouched) && untouched)
        << "a refused question wrote a result";
}

TEST_F(AuthoredQueryDispatchTest, AChangedContractIsStaleForOldHandlesAndUnboundForItsOldAnswerer)
{
    AuthoredQueryBindingToken token = Dispatcher->Bind<&Doubler::Evaluate>(Id, Answerer);
    const AuthoredQueryHandle compiledAgainst = Handle;

    ASSERT_TRUE(Declare(Registry, Question("test.double", 10)));
    ASSERT_FALSE(Registry.IsCurrent(compiledAgainst));

    AuthoredValue result;
    EXPECT_EQ(AskWith(compiledAgainst, 2, result), AuthoredQueryStatus::Stale);
    const AuthoredQueryHandle current = Registry.Resolve("test.double");
    EXPECT_EQ(AskWith(current, 2, result), AuthoredQueryStatus::Unbound);
    EXPECT_EQ(Dispatcher->Unanswered().size(), 1u);

    token = Dispatcher->Bind<&Doubler::Evaluate>(Id, Answerer);
    EXPECT_EQ(AskWith(current, 2, result), AuthoredQueryStatus::Value);
    EXPECT_TRUE(Dispatcher->Unanswered().empty());
}

TEST_F(AuthoredQueryDispatchTest, AHandleFromAnotherCatalogIsStaleThoughItsSlotMatches)
{
    AuthoredQueryBindingToken token = Dispatcher->Bind<&Doubler::Evaluate>(Id, Answerer);

    AuthoredQueryRegistry elsewhere;
    ASSERT_TRUE(Declare(elsewhere, Question("test.other")));
    const AuthoredQueryHandle foreign = elsewhere.Resolve("test.other");
    ASSERT_EQ(foreign.Slot, Handle.Slot);

    AuthoredValue result;
    EXPECT_EQ(AskWith(foreign, 2, result), AuthoredQueryStatus::Stale);
    EXPECT_EQ(Answerer.Calls, 0);
}

TEST_F(AuthoredQueryDispatchTest, AnOldTokenCannotRemoveItsReplacement)
{
    Doubler first;
    Doubler second;
    AuthoredQueryBindingToken old = Dispatcher->Bind<&Doubler::Evaluate>(Id, first);
    AuthoredQueryBindingToken replacement = Dispatcher->Bind<&Doubler::Evaluate>(Id, second);
    old.Reset();

    AuthoredValue result;
    EXPECT_EQ(Ask(1, result), AuthoredQueryStatus::Value);
    EXPECT_EQ(second.Calls, 1);
    EXPECT_EQ(first.Calls, 0);

    replacement.Reset();
    EXPECT_EQ(Ask(1, result), AuthoredQueryStatus::Unbound);
}

TEST_F(AuthoredQueryDispatchTest, ATokenOutlivingItsDispatcherIsInert)
{
    AuthoredQueryBindingToken token = Dispatcher->Bind<&Doubler::Evaluate>(Id, Answerer);
    Dispatcher.reset();
    EXPECT_FALSE(token.IsValid());
    token.Reset();
    SUCCEED();
}

TEST_F(AuthoredQueryDispatchTest, AWarmedScalarQueryAllocatesNothing)
{
    AuthoredQueryBindingToken token = Dispatcher->Bind<&Doubler::Evaluate>(Id, Answerer);
    const AuthoredValue argument = AuthoredValue::Int(3);
    AuthoredValue result;
    ASSERT_EQ(Dispatcher->Evaluate(Handle, { &argument, 1 }, result), AuthoredQueryStatus::Value);

    constexpr int kAsks = 10000;
    const std::size_t before = AllocationCount();
    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < kAsks; ++index)
        (void)Dispatcher->Evaluate(Handle, { &argument, 1 }, result);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_EQ(AllocationCount(), before) << "a warmed scalar query allocated";
    std::printf("authored query: %.1f ns per scalar evaluation (this build)\n",
                std::chrono::duration<double, std::nano>(elapsed).count() / kAsks);
}

namespace
{
enum class Breach
{
    WrongKind,
    UnlistedChoice,
    NotFinite,
    Throws,
};

struct Breaker
{
    Breach Kind = Breach::WrongKind;

    static AuthoredQueryStatus Evaluate(const Breaker& self, std::span<const AuthoredValue>,
                                        AuthoredValue& result)
    {
        switch (self.Kind)
        {
        case Breach::WrongKind: result = AuthoredValue::String("seven"); break;
        case Breach::UnlistedChoice: result = AuthoredValue::Enum("sulky"); break;
        case Breach::NotFinite: result = AuthoredValue::Float(std::numeric_limits<double>::quiet_NaN()); break;
        case Breach::Throws: throw std::runtime_error("the answerer failed");
        }
        return AuthoredQueryStatus::Value;
    }
};

[[nodiscard]] AuthoredQueryDefinition Answering(std::string name, DataFieldKind kind)
{
    AuthoredQueryDefinition definition;
    definition.Name = std::move(name);
    definition.Result.Kind = kind;
    if (kind == DataFieldKind::Enum)
    {
        definition.Result.EnumChoices = { DataEnumChoice{ .Value = "calm", .DisplayName = {}, .Description = {} } };
    }
    return definition;
}
} // namespace

TEST(AuthoredQueryResultContract, AnAnswerOutsideTheDeclarationIsAProviderDefectNotAValue)
{
    AuthoredQueryRegistry registry;
    ASSERT_TRUE(Declare(registry, Answering("test.count", DataFieldKind::Int)));
    ASSERT_TRUE(Declare(registry, Answering("test.mood", DataFieldKind::Enum)));
    ASSERT_TRUE(Declare(registry, Answering("test.ratio", DataFieldKind::Float)));
    AuthoredQueryDispatcher dispatcher(registry);

    const Breaker wrongKind{ .Kind = Breach::WrongKind };
    const Breaker unlisted{ .Kind = Breach::UnlistedChoice };
    const Breaker notFinite{ .Kind = Breach::NotFinite };
    AuthoredQueryBindingToken a =
        dispatcher.Bind<&Breaker::Evaluate>(registry.Find("test.count"), wrongKind);
    AuthoredQueryBindingToken b = dispatcher.Bind<&Breaker::Evaluate>(registry.Find("test.mood"), unlisted);
    AuthoredQueryBindingToken c =
        dispatcher.Bind<&Breaker::Evaluate>(registry.Find("test.ratio"), notFinite);

    for (const char* name : { "test.count", "test.mood", "test.ratio" })
    {
        AuthoredValue result = AuthoredValue::Bool(true);
        EXPECT_EQ(dispatcher.Evaluate(registry.Resolve(name), {}, result),
                  AuthoredQueryStatus::InvalidResult)
            << name;
        bool untouched = false;
        EXPECT_TRUE(result.TryGetBool(untouched) && untouched)
            << name << ": a refused answer reached the caller";
    }
}

TEST(AuthoredQueryResultContract, AnAnswererThatThrowsLeavesTheDispatcherUsable)
{
    AuthoredQueryRegistry registry;
    ASSERT_TRUE(Declare(registry, Answering("test.count", DataFieldKind::Int)));
    AuthoredQueryDispatcher dispatcher(registry);
    const Breaker thrower{ .Kind = Breach::Throws };
    AuthoredQueryBindingToken token =
        dispatcher.Bind<&Breaker::Evaluate>(registry.Find("test.count"), thrower);

    AuthoredValue result;
    EXPECT_THROW((void)dispatcher.Evaluate(registry.Resolve("test.count"), {}, result),
                 std::runtime_error);
    EXPECT_FALSE(dispatcher.IsEvaluating());

    const Breaker other{ .Kind = Breach::WrongKind };
    AuthoredQueryBindingToken replacement =
        dispatcher.Bind<&Breaker::Evaluate>(registry.Find("test.count"), other);
    EXPECT_TRUE(replacement.IsValid());
    token.Reset();
    replacement.Reset();
}

TEST(AuthoredQueryDefaults, TheCallerSuppliesEveryArgumentADefaultIsForTheCompilerToFill)
{
    AuthoredQueryRegistry registry;
    AuthoredQueryDefinition defaulted = Question("test.double");
    defaulted.Arguments.Children.front().Default = std::int64_t{ 4 };
    ASSERT_TRUE(Declare(registry, std::move(defaulted)));
    AuthoredQueryDispatcher dispatcher(registry);
    const Doubler answerer;
    AuthoredQueryBindingToken token =
        dispatcher.Bind<&Doubler::Evaluate>(registry.Find("test.double"), answerer);
    const AuthoredQueryHandle handle = registry.Resolve("test.double");

    AuthoredValue result;
    EXPECT_EQ(dispatcher.Evaluate(handle, {}, result), AuthoredQueryStatus::InvalidArguments);

    // Filled from the declaration, as a compiled call would.
    const DataDefaultValue& declared = registry.Get(handle.Slot)->Arguments.Children.front().Default;
    const AuthoredValue filled = AuthoredValue::Int(std::get<std::int64_t>(declared));
    ASSERT_EQ(dispatcher.Evaluate(handle, { &filled, 1 }, result), AuthoredQueryStatus::Value);
    std::int64_t answer = 0;
    ASSERT_TRUE(result.TryGetInt(answer));
    EXPECT_EQ(answer, 8);
}
