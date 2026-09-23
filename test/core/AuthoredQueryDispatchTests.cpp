// Asking an authored question: what the dispatcher checks before an
// implementation answers, what each refusal means, and what happens to a
// token or an answer that has outlived the contract it was written for.

#include "AllocationCounter.h"

#include <authored/AuthoredQueryDispatcher.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
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

// Doubles the level it is asked about; says nothing for a negative one.
struct Doubler
{
    // Counting is not simulation state; the answerer is still const to the
    // dispatcher.
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
        Dispatcher.emplace(Registry);
    }

    [[nodiscard]] AuthoredQueryStatus Ask(std::int64_t level, AuthoredValue& result,
                                          AuthoredQueryRevision expected = {}) const
    {
        const AuthoredValue argument = AuthoredValue::Int(level);
        return Dispatcher->Evaluate(Id, { &argument, 1 }, result, expected);
    }

    AuthoredQueryRegistry Registry;
    AuthoredQueryId Id;
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
    // Nothing bound.
    EXPECT_EQ(Ask(1, result), AuthoredQueryStatus::Unbound);

    AuthoredQueryBindingToken token = Dispatcher->Bind<&Doubler::Evaluate>(Id, Answerer);
    // Outside the declared range: the declaration refuses it, the answerer is
    // never asked.
    EXPECT_EQ(Ask(101, result), AuthoredQueryStatus::InvalidArguments);
    EXPECT_EQ(Answerer.Calls, 0);
    // The wrong count.
    EXPECT_EQ(Dispatcher->Evaluate(Id, {}, result), AuthoredQueryStatus::InvalidArguments);
    // An answer that does not apply is not a value.
    EXPECT_EQ(Ask(-1, result), AuthoredQueryStatus::Unavailable);
    // Not a query this catalog has.
    EXPECT_EQ(Dispatcher->Evaluate(AuthoredQueryId{ 99 }, {}, result), AuthoredQueryStatus::Stale);

    bool untouched = false;
    EXPECT_TRUE(result.TryGetBool(untouched) && untouched)
        << "a refused question wrote a result";
}

TEST_F(AuthoredQueryDispatchTest, AChangedContractIsStaleForCallersAndUnboundForItsOldAnswerer)
{
    AuthoredQueryBindingToken token = Dispatcher->Bind<&Doubler::Evaluate>(Id, Answerer);
    const AuthoredQueryRevision compiledAgainst = Registry.Revision(Id);

    // The provider narrows its range: a different contract.
    ASSERT_TRUE(Declare(Registry, Question("test.double", 10)));
    ASSERT_NE(Registry.Revision(Id), compiledAgainst);

    AuthoredValue result;
    EXPECT_EQ(Ask(2, result, compiledAgainst), AuthoredQueryStatus::Stale);
    // Asked without a revision, the implementation is the one out of date.
    EXPECT_EQ(Ask(2, result), AuthoredQueryStatus::Unbound);
    EXPECT_EQ(Dispatcher->Unanswered().size(), 1u);

    // Rebinding is the statement that it was updated.
    token = Dispatcher->Bind<&Doubler::Evaluate>(Id, Answerer);
    EXPECT_EQ(Ask(2, result, Registry.Revision(Id)), AuthoredQueryStatus::Value);
    EXPECT_TRUE(Dispatcher->Unanswered().empty());
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
    ASSERT_EQ(Dispatcher->Evaluate(Id, { &argument, 1 }, result), AuthoredQueryStatus::Value);

    constexpr int kAsks = 10000;
    const std::size_t before = AllocationCount();
    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < kAsks; ++index)
        (void)Dispatcher->Evaluate(Id, { &argument, 1 }, result);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_EQ(AllocationCount(), before) << "a warmed scalar query allocated";
    std::printf("authored query: %.1f ns per scalar evaluation (this build)\n",
                std::chrono::duration<double, std::nano>(elapsed).count() / kAsks);
}
