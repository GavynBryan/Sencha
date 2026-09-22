// The predicate language: rows that all must pass, each a test or an any-of
// group, compiled against a bound rig and evaluated with the first failing row
// named. Absent operands -- a request that is not live -- fail every
// comparison, and names that do not resolve are located errors.

#include "AnimRigFixture.h"

#include <cmath>

namespace
{
    JsonValue Parse(std::string_view text)
    {
        std::optional<JsonValue> value = JsonParse(text);
        EXPECT_TRUE(value.has_value()) << text;
        return value.value_or(JsonValue());
    }

    struct Predicates : AnimRigFixture
    {
        DataAssetHandle Rig;
        EntityId Entity;
        AnimRequestSet Requests;
        GameplayTagContainer Container;
        std::vector<std::uint32_t> Facts = std::vector<std::uint32_t>(kAnimFactsSmall, 0u);

        Predicates()
            : AnimRigFixture({ "anim.intent.reload", "stance.crouch", "stance.prone", "state.stunned",
                               "state.stunned.hard" })
        {
            (void)Load("asset://anim/p.facts.sdata", kAnimFactSchemaType, R"({
                "slots": [ { "name": "Grounded", "kind": "bool" }, { "name": "Speed", "kind": "float" },
                           { "name": "Stance", "kind": "tag" }, { "name": "Tags", "kind": "tagset" },
                           { "name": "Camera", "kind": "float", "local": true } ] })");
            (void)Load("asset://anim/p.requests.sdata", kAnimRequestSchemaType, R"({
                "intents": [ { "intent": "anim.intent.reload",
                               "params": [ { "name": "rate", "kind": "float" }, { "name": "stance", "kind": "tag" } ] } ] })");
            Rig = Load("asset://anim/p.rig.sdata", kAnimRigType, R"({
                "facts": "asset://anim/p.facts.sdata", "requests": "asset://anim/p.requests.sdata",
                "layers": [ { "name": "anim.layer.base" } ] })");
            EXPECT_TRUE(Bound(Rig).Valid) << Describe(Bound(Rig));
            Entity = Entities.CreateEntity();
        }

        AnimProgram Compile(std::string_view rows, std::vector<AnimDiagnostic>* problems = nullptr)
        {
            AnimPredicateDecl decl;
            std::string error;
            const JsonValue parsed = Parse(rows);
            EXPECT_TRUE(ReadAnimPredicate(&parsed, "$.data.enter", decl, error)) << error;
            std::vector<AnimDiagnostic> local;
            AnimProgram program = CompileAnimPredicate(decl, Bound(Rig), &Tags(), "asset://anim/p.selector.sdata",
                                                       "$.data.enter", problems != nullptr ? *problems : local);
            if (problems == nullptr)
            {
                EXPECT_TRUE(local.empty()) << FormatAnimDiagnostic(local.front());
            }
            return program;
        }

        void Set(std::string_view slot, float value)
        {
            Facts[static_cast<std::size_t>(Bound(Rig).FindSlot(slot))] = AnimFactFromFloat(value);
        }
        void Set(std::string_view slot, bool value)
        {
            Facts[static_cast<std::size_t>(Bound(Rig).FindSlot(slot))] = AnimFactFromBool(value);
        }
        void SetTag(std::string_view slot, std::string_view tag)
        {
            Facts[static_cast<std::size_t>(Bound(Rig).FindSlot(slot))] = Tag(tag).Value;
        }

        AnimPredicateResult Evaluate(const AnimProgram& program)
        {
            AnimPredicateInputs inputs;
            inputs.Facts = Facts;
            inputs.Tags = &Container;
            inputs.Registry = &Tags();
            inputs.Requests = &Requests;
            inputs.Now = Now;
            inputs.TickSeconds = kTick;
            inputs.BehaviorStartTick = 0;
            return EvaluateAnimProgram(program, inputs);
        }
    };
}

TEST(AnimPredicate, RowsAllPassAndTheFirstFailingRowIsNamed)
{
    Predicates fx;
    const AnimProgram program = fx.Compile(R"([
        { "fact": "Grounded" },
        { "fact": "Speed", "compare": "ge", "value": 2 },
        { "any": [ { "fact": "Stance", "compare": "eq", "tag": "stance.crouch" },
                   { "fact": "Stance", "compare": "eq", "tag": "stance.prone" } ] } ])");

    fx.Set("Grounded", true);
    fx.Set("Speed", 1.5f);
    const AnimPredicateResult slow = fx.Evaluate(program);
    EXPECT_FALSE(slow.Passed);
    EXPECT_EQ(slow.FailedRow, 1);
    EXPECT_DOUBLE_EQ(slow.Observed, 1.5);
    EXPECT_DOUBLE_EQ(slow.Expected, 2.0);

    fx.Set("Speed", 2.0f);
    fx.SetTag("Stance", "stance.prone");
    EXPECT_TRUE(fx.Evaluate(program).Passed);
    fx.SetTag("Stance", "stance.crouch");
    EXPECT_TRUE(fx.Evaluate(program).Passed);
    fx.Set("Grounded", false);
    EXPECT_EQ(fx.Evaluate(program).FailedRow, 0);
    EXPECT_TRUE(fx.Compile("[]").AlwaysPasses());
}

TEST(AnimPredicate, TagQueriesAreHierarchical)
{
    Predicates fx;
    const AnimProgram any = fx.Compile(R"([ { "fact": "Tags", "has": "any", "query": [ "state.stunned" ] } ])");
    const AnimProgram none = fx.Compile(R"([ { "fact": "Tags", "has": "none", "query": [ "state.stunned" ] } ])");
    EXPECT_FALSE(fx.Evaluate(any).Passed);
    EXPECT_TRUE(fx.Evaluate(none).Passed);
    ASSERT_TRUE(fx.Container.Grant(fx.Tag("state.stunned.hard")));
    EXPECT_TRUE(fx.Evaluate(any).Passed);
    EXPECT_FALSE(fx.Evaluate(none).Passed);
}

TEST(AnimPredicate, RequestTestsReadTheLayersPrimaryRecord)
{
    Predicates fx;
    const AnimProgram active = fx.Compile(R"([ { "request": "anim.intent.reload" } ])");
    const AnimProgram young = fx.Compile(
        R"([ { "request": "anim.intent.reload", "test": "age", "compare": "lt", "value": 0.5 } ])");
    const AnimProgram fast = fx.Compile(
        R"([ { "request": "anim.intent.reload", "test": "param", "param": "rate", "compare": "gt", "value": 1 } ])");
    const AnimProgram crouched = fx.Compile(
        R"([ { "request": "anim.intent.reload", "test": "param", "param": "stance", "compare": "eq", "tag": "stance.crouch" } ])");
    const AnimProgram notYoung = fx.Compile(
        R"([ { "request": "anim.intent.reload", "test": "age", "compare": "lt", "value": 0.5, "not": true } ])");
    const AnimProgram interrupted = fx.Compile(
        R"([ { "request": "anim.intent.reload", "test": "cancelled", "reason": "interrupted" } ])");
    EXPECT_TRUE(young.ReadsTime);
    EXPECT_FALSE(active.ReadsTime);
    ASSERT_EQ(active.Intents.size(), 1u);

    // No request: every comparison against it fails, not-equal included.
    EXPECT_FALSE(fx.Evaluate(active).Passed);
    EXPECT_FALSE(fx.Evaluate(young).Passed);
    EXPECT_TRUE(std::isnan(fx.Evaluate(young).Observed));
    EXPECT_TRUE(fx.Evaluate(notYoung).Passed);

    AnimRequestDesc desc;
    desc.Source = fx.Entity;
    desc.Intent = fx.Tag("anim.intent.reload");
    desc.Params[0] = AnimFactFromFloat(1.5f);
    desc.Params[1] = fx.Tag("stance.crouch").Value;
    const AnimRequestResult issued = IssueAnimRequest(fx.Requests, desc, fx.Now, nullptr);
    EXPECT_TRUE(fx.Evaluate(active).Passed);
    EXPECT_TRUE(fx.Evaluate(young).Passed);
    EXPECT_TRUE(fx.Evaluate(fast).Passed);
    EXPECT_TRUE(fx.Evaluate(crouched).Passed);

    fx.Now += 60;
    EXPECT_FALSE(fx.Evaluate(young).Passed);
    EXPECT_NEAR(fx.Evaluate(young).Observed, 1.0, 1e-9);

    ASSERT_TRUE(CancelAnimRequest(fx.Requests, issued.Id, AnimCancelReason::Interrupted, fx.Now, nullptr));
    EXPECT_TRUE(fx.Evaluate(interrupted).Passed);
    EXPECT_FALSE(fx.Evaluate(active).Passed);
    fx.Now += 1;
    EXPECT_FALSE(fx.Evaluate(interrupted).Passed);
}

TEST(AnimPredicate, UnresolvedNamesAreLocatedErrors)
{
    Predicates fx;
    std::vector<AnimDiagnostic> problems;
    (void)fx.Compile(R"([
        { "fact": "Sped", "compare": "gt", "value": 1 },
        { "fact": "Stance", "compare": "eq", "tag": "stance.flying" },
        { "request": "anim.intent.reload", "test": "param", "param": "speed", "compare": "gt", "value": 1 },
        { "any": [ { "fact": "Grounded" }, { "fact": "Tags", "has": "all", "query": [ "state.dizzy" ] } ] },
        { "fact": "Speed" } ])",
                     &problems);
    const auto at = [&](std::string_view code) -> std::string {
        for (const AnimDiagnostic& problem : problems)
            if (problem.Code == code)
                return problem.FieldPath;
        return "(missing)";
    };
    EXPECT_EQ(at("anim.predicate.unknown_fact"), "$.data.enter[0].fact");
    EXPECT_EQ(at("anim.predicate.unknown_param"), "$.data.enter[2].param");
    EXPECT_EQ(at("anim.predicate.operand"), "$.data.enter[4].compare");
    std::vector<std::string> unknownTags;
    for (const AnimDiagnostic& problem : problems)
        if (problem.Code == "anim.predicate.unknown_tag")
            unknownTags.push_back(problem.FieldPath);
    EXPECT_EQ(unknownTags, (std::vector<std::string>{ "$.data.enter[1].tag", "$.data.enter[3].any[1].query[0]" }));
    EXPECT_FALSE(fx.Tags().FindTag("state.dizzy").IsValid());
}

TEST(AnimPredicate, LocalFactsAreTracked)
{
    Predicates fx;
    EXPECT_TRUE(fx.Compile(R"([ { "fact": "Camera", "compare": "gt", "value": 0 } ])").ReadsLocalFacts);
    EXPECT_FALSE(fx.Compile(R"([ { "fact": "Speed", "compare": "gt", "value": 0 } ])").ReadsLocalFacts);
}

TEST(AnimPredicate, MalformedRowsAreRejectedWhereTheyAre)
{
    AnimPredicateDecl decl;
    std::string error;
    const JsonValue twoKinds = Parse(R"([ { "fact": "A" }, { "request": "x", "elapsed": "behavior" } ])");
    EXPECT_FALSE(ReadAnimPredicate(&twoKinds, "$.data.rules[0].enter", decl, error));
    EXPECT_EQ(error.substr(0, error.find(' ')), "$.data.rules[0].enter[1]");
    const JsonValue emptyAny = Parse(R"([ { "any": [] } ])");
    EXPECT_FALSE(ReadAnimPredicate(&emptyAny, "$.data.when", decl, error));
    EXPECT_EQ(error.substr(0, error.find(' ')), "$.data.when[0].any");
}
