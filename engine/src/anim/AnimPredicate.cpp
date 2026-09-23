#include <anim/AnimPredicate.h>

#include "AnimSchemaFields.h"

#include <anim/AnimFacts.h>
#include <anim/AnimRequests.h>
#include <anim/AnimRigBinding.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <optional>

namespace
{
    constexpr std::array<std::string_view, 6> kCompareNames{ "lt", "le", "gt", "ge", "eq", "ne" };
    constexpr std::array<std::string_view, 3> kMatchNames{ "all", "any", "none" };
    constexpr std::array<std::string_view, 4> kTestNames{ "active", "age", "param", "cancelled" };
    constexpr std::array<std::string_view, 5> kReasonNames{ "none", "released", "interrupted",
                                                            "failed", "superseded" };
    constexpr double kAbsent = std::numeric_limits<double>::quiet_NaN();

    template <std::size_t N>
    std::optional<std::size_t> IndexOf(const std::array<std::string_view, N>& names,
                                       std::string_view value)
    {
        const auto it = std::find(names.begin(), names.end(), value);
        if (it == names.end())
            return std::nullopt;
        return static_cast<std::size_t>(it - names.begin());
    }

    std::vector<DataFieldSchema> TestMembers()
    {
        using AnimSchema::ArrayOf;
        using AnimSchema::DataRef;
        using AnimSchema::Enum;
        using AnimSchema::Field;
        using AnimSchema::Record;
        const auto optional = [](DataFieldSchema field) {
            field.Required = false;
            return field;
        };
        std::vector<DataFieldSchema> members;
        members.push_back(optional(Field("fact", DataFieldKind::String, "Fact",
                                         "The fact slot this test reads.")));
        members.push_back(optional(Enum("compare", "Comparison", "How the value is compared.",
                                        { { "lt", "<", {} }, { "le", "<=", {} }, { "gt", ">", {} },
                                          { "ge", ">=", {} }, { "eq", "==", {} }, { "ne", "!=", {} } })));
        members.push_back(optional(Field("value", DataFieldKind::Float, "Value",
                                         "The number the value is compared against.")));
        members.push_back(optional(Field("tag", DataFieldKind::GameplayTag, "Tag",
                                         "The tag a tag-valued fact or parameter is compared against.")));
        members.push_back(optional(Enum("has", "Tag match", "How the entity's tags are matched.",
                                        { { "all", "All of", "Every query tag, or a descendant" },
                                          { "any", "Any of", "At least one query tag" },
                                          { "none", "None of", "No query tag" } })));
        DataFieldSchema query = ArrayOf("query", "Query", "Tags matched hierarchically.",
                                        Field({}, DataFieldKind::GameplayTag, "Tag", {}));
        members.push_back(std::move(query));
        members.push_back(optional(Field("request", DataFieldKind::GameplayTag, "Request intent",
                                         "The intent of a request this test reads.")));
        members.push_back(optional(Enum("test", "Request test", "What is read of the request.",
                                        { { "active", "Active", "A live request claims this layer" },
                                          { "age", "Age", "Seconds since the request started" },
                                          { "param", "Parameter", "A named request parameter" },
                                          { "cancelled", "Cancelled", "Cancelled on this tick" } })));
        members.push_back(optional(Field("param", DataFieldKind::String, "Parameter",
                                         "The request parameter compared.")));
        members.push_back(optional(Enum("reason", "Cancel reason", "The reason to match; any when absent.",
                                        { { "released", "Released", {} }, { "interrupted", "Interrupted", {} },
                                          { "failed", "Failed", {} }, { "superseded", "Superseded", {} } })));
        members.push_back(optional(Enum("elapsed", "Elapsed", "Time this layer's behavior has been winning.",
                                        { { "behavior", "In behavior", "Seconds since the behavior won" } })));
        members.push_back(optional(Field("not", DataFieldKind::Bool, "Negate",
                                         "Pass when the test fails.")));
        return members;
    }

    double ToNumber(AnimFactKind kind, std::uint32_t bits)
    {
        return kind == AnimFactKind::Tag ? static_cast<double>(bits)
                                         : static_cast<double>(AnimFactToNumber(kind, bits));
    }

    AnimFactKind ParamFactKind(AnimRequestParamKind kind)
    {
        switch (kind)
        {
        case AnimRequestParamKind::Float: return AnimFactKind::Float;
        case AnimRequestParamKind::Int: return AnimFactKind::Int;
        case AnimRequestParamKind::Bool: return AnimFactKind::Bool;
        case AnimRequestParamKind::Tag: return AnimFactKind::Tag;
        }
        return AnimFactKind::Float;
    }

    // Absent (NaN) fails every comparison, not-equal included: a request that
    // is not live has no age that differs from anything.
    bool CompareValues(AnimCompareOp op, double lhs, double rhs)
    {
        if (std::isnan(lhs) || std::isnan(rhs))
            return false;
        switch (op)
        {
        case AnimCompareOp::Lt: return lhs < rhs;
        case AnimCompareOp::Le: return lhs <= rhs;
        case AnimCompareOp::Gt: return lhs > rhs;
        case AnimCompareOp::Ge: return lhs >= rhs;
        case AnimCompareOp::Eq: return lhs == rhs;
        case AnimCompareOp::Ne: return lhs != rhs;
        }
        return false;
    }

    bool ReadTest(const JsonValue& value, const std::string& at, AnimPredicateTest& out,
                  std::string& error)
    {
        if (!value.IsObject())
        {
            error = at + " A test is an object.";
            return false;
        }
        const auto text = [&](std::string_view key) -> const std::string* {
            const JsonValue* found = value.Find(key);
            return found != nullptr && found->IsString() ? &found->AsString() : nullptr;
        };
        if (const JsonValue* negate = value.Find("not"); negate != nullptr && negate->IsBool())
            out.Negate = negate->AsBool();
        // Enumerations are checked here as well as by the schema: an editor
        // reads working documents that have not been validated yet.
        const auto choice = [&](std::string_view key, const auto& names, std::size_t& outIndex) {
            const std::string* chosen = text(key);
            if (chosen == nullptr)
                return true;
            const std::optional<std::size_t> index = IndexOf(names, *chosen);
            if (!index)
            {
                error = std::format("{}.{} '{}' is not one of the allowed values.", at, key, *chosen);
                return false;
            }
            outIndex = *index;
            return true;
        };
        std::size_t compare = kCompareNames.size();
        if (!choice("compare", kCompareNames, compare))
            return false;
        if (compare != kCompareNames.size())
        {
            out.HasCompare = true;
            out.Compare = static_cast<AnimCompareOp>(compare);
        }
        if (const JsonValue* number = value.Find("value"); number != nullptr && number->IsNumber())
            out.Value = number->AsNumber();
        if (const std::string* tag = text("tag"))
            out.Tag = *tag;

        const int kinds = (value.Find("request") != nullptr ? 1 : 0)
            + (value.Find("elapsed") != nullptr ? 1 : 0)
            + (value.Find("has") != nullptr ? 1 : 0);
        if (kinds > 1)
        {
            error = at + " A test reads one thing: a fact, the entity's tags, a request, or "
                         "elapsed time.";
            return false;
        }

        if (const std::string* intent = text("request"))
        {
            out.Kind = AnimTestKind::Request;
            out.Intent = *intent;
            std::size_t test = static_cast<std::size_t>(AnimRequestTest::Active);
            std::size_t reason = static_cast<std::size_t>(AnimCancelReason::None);
            if (!choice("test", kTestNames, test) || !choice("reason", kReasonNames, reason))
                return false;
            out.Test = static_cast<AnimRequestTest>(test);
            out.Reason = static_cast<AnimCancelReason>(reason);
            if (const std::string* param = text("param"))
                out.Param = *param;
            return true;
        }
        if (value.Find("elapsed") != nullptr)
        {
            out.Kind = AnimTestKind::Elapsed;
            if (!out.HasCompare)
            {
                error = at + ".compare Elapsed time is compared against a number of seconds.";
                return false;
            }
            return true;
        }
        const std::string* fact = text("fact");
        if (fact == nullptr)
        {
            error = at + " A test names a fact, a request, or elapsed time.";
            return false;
        }
        out.Fact = *fact;
        if (text("has") != nullptr)
        {
            out.Kind = AnimTestKind::Tags;
            std::size_t match = 0;
            if (!choice("has", kMatchNames, match))
                return false;
            out.Match = static_cast<AnimTagMatch>(match);
            if (const JsonValue* query = value.Find("query"); query != nullptr && query->IsArray())
            {
                for (const JsonValue& tag : query->AsArray())
                    if (tag.IsString())
                        out.Query.push_back(tag.AsString());
            }
            if (out.Query.empty())
            {
                error = at + ".query A tag test names at least one tag.";
                return false;
            }
            return true;
        }
        out.Kind = AnimTestKind::Fact;
        return true;
    }

    // Emits into one program with located problems.
    struct Compiler
    {
        const AnimBoundRig& Rig;
        const GameplayTagRegistry* Tags;
        const std::string& Asset;
        std::vector<AnimDiagnostic>& Diagnostics;
        AnimProgram Program;

        void Error(std::string code, std::string field, std::string message)
        {
            Diagnostics.push_back(AnimDiagnostic{ AnimDiagnosticSeverity::Error, std::move(code),
                                                  Asset, std::move(field), std::move(message) });
        }

        void Emit(AnimOp op, std::uint32_t operand = 0, std::uint8_t mode = 0, double constant = 0.0,
                  std::uint32_t index = 0)
        {
            Program.Ops.push_back(AnimOpcode{ op, mode, operand, index, constant });
        }

        std::optional<GameplayTagId> Tag(const std::string& name, const std::string& field)
        {
            const GameplayTagId id = Tags != nullptr ? Tags->FindTag(name) : GameplayTagId{};
            if (!id.IsValid())
            {
                Error("anim.predicate.unknown_tag", field,
                      std::format("'{}' is not a gameplay tag this World declares.", name));
                return std::nullopt;
            }
            return id;
        }

        // A comparand for a value of `kind`: the test's tag for tag values,
        // its number otherwise.
        bool EmitComparand(const AnimPredicateTest& test, AnimFactKind kind, const std::string& at)
        {
            if (kind == AnimFactKind::Tag)
            {
                if (test.Tag.empty())
                {
                    Error("anim.predicate.operand", at + ".tag", "A tag value is compared against a tag.");
                    return false;
                }
                if (test.Compare != AnimCompareOp::Eq && test.Compare != AnimCompareOp::Ne)
                {
                    Error("anim.predicate.operand", at + ".compare", "A tag is only equal or not equal.");
                    return false;
                }
                const std::optional<GameplayTagId> tag = Tag(test.Tag, at + ".tag");
                if (!tag)
                    return false;
                Emit(AnimOp::PushConst, 0, 0, static_cast<double>(tag->Value));
                return true;
            }
            Emit(AnimOp::PushConst, 0, 0, test.Value);
            return true;
        }

        bool CompileTest(const AnimPredicateTest& test, const std::string& at)
        {
            switch (test.Kind)
            {
            case AnimTestKind::Fact:
            {
                const int slot = Rig.FindSlot(test.Fact);
                if (slot < 0)
                {
                    Error("anim.predicate.unknown_fact", at + ".fact",
                          std::format("'{}' is not a fact of this rig.", test.Fact));
                    return false;
                }
                const AnimBoundFactSlot& bound = Rig.Slots[static_cast<std::size_t>(slot)];
                Program.ReadsLocalFacts = Program.ReadsLocalFacts || bound.Local;
                if (bound.Kind == AnimFactKind::TagSet)
                {
                    Error("anim.predicate.operand", at + ".has",
                          std::format("'{}' is a tag set; test it with has and query.", test.Fact));
                    return false;
                }
                Emit(AnimOp::PushSlot, static_cast<std::uint32_t>(slot),
                     static_cast<std::uint8_t>(bound.Kind));
                if (!test.HasCompare)
                {
                    if (bound.Kind != AnimFactKind::Bool)
                    {
                        Error("anim.predicate.operand", at + ".compare",
                              std::format("'{}' is a {}; compare it against a value.", test.Fact,
                                          AnimFactKindName(bound.Kind)));
                        return false;
                    }
                    return true;
                }
                if (!EmitComparand(test, bound.Kind, at))
                    return false;
                Emit(AnimOp::Compare, 0, static_cast<std::uint8_t>(test.Compare));
                return true;
            }
            case AnimTestKind::Tags:
            {
                const int slot = Rig.FindSlot(test.Fact);
                if (slot < 0 || Rig.Slots[static_cast<std::size_t>(slot)].Kind != AnimFactKind::TagSet)
                {
                    Error("anim.predicate.unknown_fact", at + ".fact",
                          std::format("'{}' is not a tag-set fact of this rig.", test.Fact));
                    return false;
                }
                std::vector<GameplayTagId> query;
                for (std::size_t i = 0; i < test.Query.size(); ++i)
                {
                    const std::optional<GameplayTagId> tag =
                        Tag(test.Query[i], std::format("{}.query[{}]", at, i));
                    if (!tag)
                        return false;
                    query.push_back(*tag);
                }
                Program.Queries.push_back(std::move(query));
                Emit(AnimOp::TagQuery, static_cast<std::uint32_t>(Program.Queries.size() - 1),
                     static_cast<std::uint8_t>(test.Match));
                return true;
            }
            case AnimTestKind::Request:
            {
                const std::optional<GameplayTagId> intent = Tag(test.Intent, at + ".request");
                if (!intent)
                    return false;
                const AnimBoundIntent* declared = Rig.FindIntent(*intent);
                if (Rig.HasRequestSchema && declared == nullptr)
                {
                    Error("anim.predicate.undeclared_intent", at + ".request",
                          std::format("'{}' is not an intent this rig's request schema declares.",
                                      test.Intent));
                    return false;
                }
                if (std::find(Program.Intents.begin(), Program.Intents.end(), *intent)
                    == Program.Intents.end())
                    Program.Intents.push_back(*intent);
                switch (test.Test)
                {
                case AnimRequestTest::Active:
                    Emit(AnimOp::ReqActive, intent->Value);
                    return true;
                case AnimRequestTest::Age:
                    if (!test.HasCompare)
                    {
                        Error("anim.predicate.operand", at + ".compare",
                              "A request's age is compared against a number of seconds.");
                        return false;
                    }
                    Program.ReadsTime = true;
                    Emit(AnimOp::PushReqAge, intent->Value);
                    Emit(AnimOp::PushConst, 0, 0, test.Value);
                    Emit(AnimOp::Compare, 0, static_cast<std::uint8_t>(test.Compare));
                    return true;
                case AnimRequestTest::Param:
                {
                    std::size_t index = 0;
                    while (declared != nullptr && index < declared->Params.size()
                           && declared->Params[index].Name != test.Param)
                        ++index;
                    if (declared == nullptr || index == declared->Params.size())
                    {
                        Error("anim.predicate.unknown_param", at + ".param",
                              std::format("'{}' declares no parameter '{}'.", test.Intent, test.Param));
                        return false;
                    }
                    if (!test.HasCompare)
                    {
                        Error("anim.predicate.operand", at + ".compare",
                              "A request parameter is compared against a value.");
                        return false;
                    }
                    const AnimFactKind kind = ParamFactKind(declared->Params[index].Kind);
                    Emit(AnimOp::PushReqParam, intent->Value, static_cast<std::uint8_t>(kind), 0.0,
                         static_cast<std::uint32_t>(index));
                    if (!EmitComparand(test, kind, at))
                        return false;
                    Emit(AnimOp::Compare, 0, static_cast<std::uint8_t>(test.Compare));
                    return true;
                }
                case AnimRequestTest::Cancelled:
                    Emit(AnimOp::PushReqCancelReason, intent->Value);
                    Emit(AnimOp::PushConst, 0, 0, static_cast<double>(test.Reason));
                    Emit(AnimOp::Compare, 0,
                         static_cast<std::uint8_t>(test.Reason == AnimCancelReason::None
                                                       ? AnimCompareOp::Ne
                                                       : AnimCompareOp::Eq));
                    return true;
                }
                return false;
            }
            case AnimTestKind::Elapsed:
                Program.ReadsTime = true;
                Emit(AnimOp::PushTimeInBehavior);
                Emit(AnimOp::PushConst, 0, 0, test.Value);
                Emit(AnimOp::Compare, 0, static_cast<std::uint8_t>(test.Compare));
                return true;
            }
            return false;
        }
    };
}

DataFieldSchema AnimPredicateSchema(std::string key, std::string display, std::string summary)
{
    using AnimSchema::ArrayOf;
    using AnimSchema::DataRef;
    using AnimSchema::Enum;
    using AnimSchema::Field;
    using AnimSchema::Record;
    DataFieldSchema test = Record({}, "Test", {}, TestMembers());
    std::vector<DataFieldSchema> rowMembers = TestMembers();
    rowMembers.push_back(ArrayOf("any", "Any of", "Passes when any of these tests passes.", test));
    DataFieldSchema row = Record({}, "Row", "A test, or an any-of group of tests.", std::move(rowMembers));
    DataFieldSchema field = ArrayOf(std::move(key), std::move(display), std::move(summary), std::move(row));
    field.Editor.Widget = "cards";
    return field;
}

bool ReadAnimPredicate(const JsonValue* rows, const std::string& path, AnimPredicateDecl& out,
                       std::string& error)
{
    out.Rows.clear();
    if (rows == nullptr)
        return true;
    if (!rows->IsArray())
    {
        error = path + " A predicate is a list of rows.";
        return false;
    }
    for (std::size_t i = 0; i < rows->AsArray().size(); ++i)
    {
        const JsonValue& entry = rows->AsArray()[i];
        const std::string at = std::format("{}[{}]", path, i);
        AnimPredicateRow row;
        if (const JsonValue* any = entry.Find("any"))
        {
            if (!any->IsArray() || any->AsArray().empty() || any->AsArray().size() > kAnimMaxAnyOf)
            {
                error = std::format("{}.any An any-of row groups one to {} tests.", at, kAnimMaxAnyOf);
                return false;
            }
            for (std::size_t t = 0; t < any->AsArray().size(); ++t)
            {
                AnimPredicateTest test;
                if (!ReadTest(any->AsArray()[t], std::format("{}.any[{}]", at, t), test, error))
                    return false;
                row.AnyOf.push_back(std::move(test));
            }
        }
        else
        {
            AnimPredicateTest test;
            if (!ReadTest(entry, at, test, error))
                return false;
            row.AnyOf.push_back(std::move(test));
        }
        out.Rows.push_back(std::move(row));
    }
    return true;
}

AnimProgram CompileAnimPredicate(const AnimPredicateDecl& decl, const AnimBoundRig& rig,
                                 const GameplayTagRegistry* tags, const std::string& asset,
                                 const std::string& path, std::vector<AnimDiagnostic>& diagnostics)
{
    Compiler compiler{ rig, tags, asset, diagnostics, {} };
    for (std::size_t r = 0; r < decl.Rows.size(); ++r)
    {
        const AnimPredicateRow& row = decl.Rows[r];
        const bool grouped = row.AnyOf.size() > 1;
        for (std::size_t t = 0; t < row.AnyOf.size(); ++t)
        {
            const AnimPredicateTest& test = row.AnyOf[t];
            const std::string at = grouped ? std::format("{}[{}].any[{}]", path, r, t)
                                           : std::format("{}[{}]", path, r);
            if (!compiler.CompileTest(test, at))
            {
                // A row that did not compile never passes, so the rule it
                // belongs to cannot win on a half-understood condition.
                compiler.Emit(AnimOp::PushConst, 0, 0, 0.0);
            }
            else if (test.Negate)
            {
                compiler.Emit(AnimOp::Not);
            }
            if (t > 0)
                compiler.Emit(AnimOp::Or);
        }
        compiler.Program.RowEnds.push_back(static_cast<std::uint32_t>(compiler.Program.Ops.size()));
    }
    return std::move(compiler.Program);
}

AnimProgram ConcatAnimPrograms(const AnimProgram& first, const AnimProgram& second)
{
    AnimProgram out = first;
    const auto opBase = static_cast<std::uint32_t>(out.Ops.size());
    const auto queryBase = static_cast<std::uint32_t>(out.Queries.size());
    for (AnimOpcode op : second.Ops)
    {
        if (op.Op == AnimOp::TagQuery)
            op.Operand += queryBase;
        out.Ops.push_back(op);
    }
    for (const std::uint32_t end : second.RowEnds)
        out.RowEnds.push_back(end + opBase);
    out.Queries.insert(out.Queries.end(), second.Queries.begin(), second.Queries.end());
    for (const GameplayTagId intent : second.Intents)
    {
        if (std::find(out.Intents.begin(), out.Intents.end(), intent) == out.Intents.end())
            out.Intents.push_back(intent);
    }
    out.ReadsTime = out.ReadsTime || second.ReadsTime;
    out.ReadsLocalFacts = out.ReadsLocalFacts || second.ReadsLocalFacts;
    return out;
}

AnimPredicateResult EvaluateAnimProgram(const AnimProgram& program, const AnimPredicateInputs& inputs)
{
    AnimPredicateResult result;
    // A row pushes at most two values per test and folds each test into one,
    // so its depth never exceeds the any-of cap plus one.
    std::array<double, kAnimMaxAnyOf + 2> stack{};
    std::uint32_t begin = 0;
    for (std::size_t row = 0; row < program.RowEnds.size(); ++row)
    {
        std::size_t top = 0;
        double observed = kAbsent;
        double expected = kAbsent;
        const auto push = [&](double value) {
            if (top < stack.size())
                stack[top++] = value;
        };
        const auto pop = [&]() { return top > 0 ? stack[--top] : 0.0; };

        for (std::uint32_t i = begin; i < program.RowEnds[row]; ++i)
        {
            const AnimOpcode& op = program.Ops[i];
            switch (op.Op)
            {
            case AnimOp::PushSlot:
                push(op.Operand < inputs.Facts.size()
                         ? ToNumber(static_cast<AnimFactKind>(op.Mode), inputs.Facts[op.Operand])
                         : 0.0);
                break;
            case AnimOp::PushConst:
                push(op.Constant);
                break;
            case AnimOp::PushReqAge:
            {
                const AnimRequest* request = inputs.Requests != nullptr
                    ? FindPrimaryAnimRequest(*inputs.Requests, GameplayTagId{ op.Operand }, inputs.Now,
                                             inputs.LayerBit)
                    : nullptr;
                push(request != nullptr
                         ? static_cast<double>(inputs.Now - request->StartTick) * inputs.TickSeconds
                         : kAbsent);
                break;
            }
            case AnimOp::PushReqParam:
            {
                const AnimRequest* request = inputs.Requests != nullptr
                    ? FindPrimaryAnimRequest(*inputs.Requests, GameplayTagId{ op.Operand }, inputs.Now,
                                             inputs.LayerBit)
                    : nullptr;
                push(request != nullptr && op.Index < kAnimRequestParams
                         ? ToNumber(static_cast<AnimFactKind>(op.Mode), request->Params[op.Index])
                         : kAbsent);
                break;
            }
            case AnimOp::PushReqCancelReason:
                push(inputs.Requests != nullptr
                         ? static_cast<double>(FindAnimCancelReason(*inputs.Requests,
                                                                    GameplayTagId{ op.Operand },
                                                                    inputs.Now))
                         : 0.0);
                break;
            case AnimOp::PushTimeInBehavior:
                push(inputs.Now >= inputs.BehaviorStartTick
                         ? static_cast<double>(inputs.Now - inputs.BehaviorStartTick) * inputs.TickSeconds
                         : 0.0);
                break;
            case AnimOp::ReqActive:
                push(inputs.Requests != nullptr
                             && FindPrimaryAnimRequest(*inputs.Requests, GameplayTagId{ op.Operand },
                                                       inputs.Now, inputs.LayerBit)
                                    != nullptr
                         ? 1.0
                         : 0.0);
                break;
            case AnimOp::TagQuery:
            {
                const std::vector<GameplayTagId>& query = program.Queries[op.Operand];
                const auto has = [&](GameplayTagId tag) {
                    return inputs.Tags != nullptr && inputs.Registry != nullptr
                        && inputs.Tags->HasDescendantOf(*inputs.Registry, tag);
                };
                bool passed = false;
                switch (static_cast<AnimTagMatch>(op.Mode))
                {
                case AnimTagMatch::All: passed = std::all_of(query.begin(), query.end(), has); break;
                case AnimTagMatch::Any: passed = std::any_of(query.begin(), query.end(), has); break;
                case AnimTagMatch::None: passed = std::none_of(query.begin(), query.end(), has); break;
                }
                push(passed ? 1.0 : 0.0);
                break;
            }
            case AnimOp::Compare:
            {
                const double rhs = pop();
                const double lhs = pop();
                observed = lhs;
                expected = rhs;
                push(CompareValues(static_cast<AnimCompareOp>(op.Mode), lhs, rhs) ? 1.0 : 0.0);
                break;
            }
            case AnimOp::Not:
                push(pop() != 0.0 ? 0.0 : 1.0);
                break;
            case AnimOp::And:
            {
                const double rhs = pop();
                const double lhs = pop();
                push(lhs != 0.0 && rhs != 0.0 ? 1.0 : 0.0);
                break;
            }
            case AnimOp::Or:
            {
                const double rhs = pop();
                const double lhs = pop();
                push(lhs != 0.0 || rhs != 0.0 ? 1.0 : 0.0);
                break;
            }
            }
        }
        begin = program.RowEnds[row];
        const double value = top > 0 ? stack[top - 1] : 0.0;
        if (value == 0.0 || std::isnan(value))
        {
            result.Passed = false;
            result.FailedRow = static_cast<int>(row);
            result.Observed = observed;
            result.Expected = expected;
            return result;
        }
    }
    return result;
}

bool AnimPredicateRequiresRequest(const AnimPredicateDecl& decl)
{
    return std::any_of(decl.Rows.begin(), decl.Rows.end(), [](const AnimPredicateRow& row) {
        return !row.AnyOf.empty() && std::all_of(row.AnyOf.begin(), row.AnyOf.end(), [](const AnimPredicateTest& test) {
                   return test.Kind == AnimTestKind::Request && !test.Negate;
               });
    });
}
