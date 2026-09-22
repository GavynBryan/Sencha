#pragma once

#include <anim/AnimDiagnostic.h>
#include <anim/AnimFactSchema.h>
#include <anim/AnimRequestSet.h>
#include <anim/AnimTypes.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct GameplayTagContainer;
class GameplayTagRegistry;
struct AnimBoundRig;

//=============================================================================
// AnimPredicate
//
// The one condition language of animation rules: selector enter and stay
// predicates and slot-map row predicates. Authored as rows that must all pass;
// a row is one test, optionally negated, or an any-of group of tests. That is
// the whole grammar -- conjunctive normal form, complete for boolean
// conditions -- and it is what the schema validates, what the editor builds,
// and what "the first row that failed" explains.
//
// A test reads a fact, the entity's tag container, a request, or how long the
// layer's behavior has been winning. Nothing else is nameable: there is no
// operand for a rule, a behavior, a previous winner or content, so no rule can
// become an edge to another.
//
// Rows compile per World into a closed stack program over slot indices and tag
// ids, and evaluate over one entity's facts with no allocation.
//=============================================================================

enum class AnimTestKind : std::uint8_t
{
    Fact,
    Tags,
    Request,
    Elapsed,
};

enum class AnimTagMatch : std::uint8_t
{
    All,
    Any,
    None,
};

enum class AnimRequestTest : std::uint8_t
{
    Active,
    Age,
    Param,
    Cancelled,
};

struct AnimPredicateTest
{
    AnimTestKind Kind = AnimTestKind::Fact;
    bool Negate = false;

    // Fact: a slot. Tags: the tagset slot.
    std::string Fact;
    // Fact, Request Age/Param, Elapsed. Absent on a bool fact read as itself.
    bool HasCompare = false;
    AnimCompareOp Compare = AnimCompareOp::Gt;
    double Value = 0.0;
    // A tag operand, by name: a tag fact's comparand, a tag param's comparand.
    std::string Tag;

    AnimTagMatch Match = AnimTagMatch::Any;
    std::vector<std::string> Query;

    std::string Intent;
    AnimRequestTest Test = AnimRequestTest::Active;
    std::string Param;
    // Cancelled: the reason to match, or None for any.
    AnimCancelReason Reason = AnimCancelReason::None;
};

struct AnimPredicateRow
{
    // One test, or an any-of group.
    std::vector<AnimPredicateTest> AnyOf;
};

struct AnimPredicateDecl
{
    // Every row must pass. No rows always passes.
    std::vector<AnimPredicateRow> Rows;
};

// The schema of one predicate field, for the data subtypes that carry one.
[[nodiscard]] DataFieldSchema AnimPredicateSchema(std::string key, std::string display,
                                                  std::string summary);

// Reads a predicate from its authored array. False with `error` naming the
// field ("$.data.rules[2].enter[1].compare ...") on a malformed row.
[[nodiscard]] bool ReadAnimPredicate(const JsonValue* rows, const std::string& path,
                                     AnimPredicateDecl& out, std::string& error);

enum class AnimOp : std::uint8_t
{
    PushSlot,
    PushConst,
    PushReqAge,
    PushReqParam,
    PushReqCancelReason,
    PushTimeInBehavior,
    ReqActive,
    TagQuery,
    Compare,
    Not,
    And,
    Or,
};

struct AnimOpcode
{
    AnimOp Op = AnimOp::PushConst;
    // Compare: the comparison. PushSlot, PushReqParam: the value's fact
    // kind. TagQuery: the match.
    std::uint8_t Mode = 0;
    // PushSlot: slot. PushReq*/ReqActive: intent tag id. PushReqParam: also
    // the parameter index in Index. TagQuery: query index.
    std::uint32_t Operand = 0;
    std::uint32_t Index = 0;
    double Constant = 0.0;
};

struct AnimProgram
{
    std::vector<AnimOpcode> Ops;
    // One past the last op of each row.
    std::vector<std::uint32_t> RowEnds;
    std::vector<std::vector<GameplayTagId>> Queries;
    // Intents the program reads, so a latch can tell which request armed it.
    std::vector<GameplayTagId> Intents;
    // True when the result can change with time alone: a request age, how
    // long the behavior has been winning. Such a program is evaluated every
    // tick rather than only when its inputs change.
    bool ReadsTime = false;
    bool ReadsLocalFacts = false;

    [[nodiscard]] bool AlwaysPasses() const { return RowEnds.empty(); }
};

// The most tests one any-of row may group, which bounds a row's stack depth.
inline constexpr std::size_t kAnimMaxAnyOf = 8;

// Compiles against a bound rig's fact layout and request schema and the
// World's vocabulary. Problems are appended to `diagnostics`, located at
// `asset` and `path`; nothing is registered on the content's behalf.
[[nodiscard]] AnimProgram CompileAnimPredicate(const AnimPredicateDecl& decl,
                                               const AnimBoundRig& rig,
                                               const GameplayTagRegistry* tags,
                                               const std::string& asset,
                                               const std::string& path,
                                               std::vector<AnimDiagnostic>& diagnostics);

// `first`'s rows followed by `second`'s: both must pass. How a delegated rule
// inherits its parent's condition.
[[nodiscard]] AnimProgram ConcatAnimPrograms(const AnimProgram& first, const AnimProgram& second);

struct AnimPredicateInputs
{
    std::span<const std::uint32_t> Facts;
    const GameplayTagContainer* Tags = nullptr;
    const GameplayTagRegistry* Registry = nullptr;
    const AnimRequestSet* Requests = nullptr;
    AnimTick Now = 0;
    double TickSeconds = 1.0 / 60.0;
    // The evaluating layer's bit, for request layer masks.
    std::uint8_t LayerBit = 1;
    AnimTick BehaviorStartTick = 0;
};

struct AnimPredicateResult
{
    bool Passed = true;
    // The first row that failed, or -1.
    int FailedRow = -1;
    // The last comparison evaluated in the failed row: what was read and what
    // it was compared against. NaN when the row had no comparison, or read
    // something absent (a request that is not live).
    double Observed = 0.0;
    double Expected = 0.0;
};

[[nodiscard]] AnimPredicateResult EvaluateAnimProgram(const AnimProgram& program,
                                                      const AnimPredicateInputs& inputs);
