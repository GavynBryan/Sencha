#pragma once

#include <authored/AuthoredQueryId.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbBinding.h>
#include <core/json/JsonValue.h>
#include <ecs/ComponentId.h>
#include <gameplay_tags/GameplayTagQuery.h>
#include <gameplay_tags/GameplayTagQueryNames.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// An authored response: conditions, then verbs in order, reading the ports its
// producer offers. Desc types are what content says, World-independent; the
// Compiled ones are what one World made of it. docs/gameplay/triggers.md.

// What a producer offers a sequence to read, by name.
enum class SequencePortKind : std::uint8_t
{
    Entity,
    Vector,
};

struct SequencePort
{
    std::string_view Name;
    SequencePortKind Kind = SequencePortKind::Entity;
};

inline constexpr std::uint8_t kNoSequencePort = 0xFF;

enum class SequenceValueSource : std::uint8_t
{
    // A verb-binding constant: {const}, {tag}, {data}, {asset}, {entity}.
    Constant,
    // {port: name}, read from the producer.
    Port,
    // {query: name, arguments: {...}, member: name}, asked when the command runs.
    Query,
};

struct SequenceArgumentDesc;

struct SequenceValueDesc
{
    SequenceValueSource Source = SequenceValueSource::Constant;
    VerbBindingArgument Constant;
    // The port's or the query's name.
    std::string Name;
    std::vector<SequenceArgumentDesc> Arguments;
    // A record answer's member the value is.
    std::string Member;
    // A vector given in this entity port's frame, turned into world space.
    std::string Frame;
    std::optional<double> Scale;

    [[nodiscard]] bool IsPlainConstant() const
    {
        return Source == SequenceValueSource::Constant && Frame.empty() && !Scale;
    }
};

struct SequenceArgumentDesc
{
    std::string Key;
    SequenceValueDesc Value;
};

enum class SequenceConditionKind : std::uint8_t
{
    // An entity port carries every listed component and matches a tag query.
    Entity,
    // A boolean query's answer is the one expected.
    Query,
};

struct SequenceConditionDesc
{
    SequenceConditionKind Kind = SequenceConditionKind::Entity;
    std::string Port;
    std::vector<std::string> Components;
    GameplayTagQueryNames Tags;
    std::string Query;
    std::vector<SequenceArgumentDesc> Arguments;
    bool Expect = true;
};

enum class SequenceFailurePolicy : std::uint8_t
{
    Continue,
    Stop,
};

struct SequenceCommandDesc
{
    std::string Verb;
    std::vector<SequenceArgumentDesc> Arguments;
    SequenceFailurePolicy OnFailure = SequenceFailurePolicy::Continue;
};

struct VerbSequenceDesc
{
    std::vector<SequenceConditionDesc> Conditions;
    std::vector<SequenceCommandDesc> Commands;
};

// The authored form: {"conditions": [...], "commands": [...]}. Every element is
// read even after one fails, so each problem is reported; a malformed element is
// left out of `out`. Asset and data references are appended to `dependencies`.
bool ReadVerbSequence(const JsonValue& authored,
                      VerbSequenceDesc& out,
                      std::vector<AssetRef>& dependencies,
                      std::vector<std::string>& errors);

[[nodiscard]] JsonValue EmptyVerbSequence();

struct CompiledSequenceValue
{
    SequenceValueSource Source = SequenceValueSource::Constant;
    AuthoredValue Constant;
    std::uint8_t Port = kNoSequencePort;
    AuthoredQueryHandle Query;
    std::vector<CompiledSequenceValue> Arguments;
    std::int32_t Member = -1;
    std::uint8_t Frame = kNoSequencePort;
    std::optional<float> Scale;
};

struct CompiledSequenceCondition
{
    SequenceConditionKind Kind = SequenceConditionKind::Entity;
    // Refused when the World it compiled against could not resolve it.
    bool Resolved = false;
    std::uint8_t Port = kNoSequencePort;
    std::vector<ComponentId> Components;
    GameplayTagQuery Tags;
    bool HasTags = false;
    AuthoredQueryHandle Query;
    std::vector<CompiledSequenceValue> Arguments;
    bool Expect = true;
};

struct CompiledSequenceCommand
{
    std::string Verb;
    // False when the verb or an argument did not compile: running it fails the
    // command, under its failure policy, rather than skipping it unseen.
    bool Resolved = false;
    CompiledVerbBinding Binding;
    // One per binding input, in the binding's input order.
    std::vector<CompiledSequenceValue> Inputs;
    SequenceFailurePolicy OnFailure = SequenceFailurePolicy::Continue;
};

struct CompiledVerbSequence
{
    std::vector<CompiledSequenceCondition> Conditions;
    std::vector<CompiledSequenceCommand> Commands;
};
