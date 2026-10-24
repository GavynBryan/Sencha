#pragma once

#include <authored/VerbDispatcher.h>
#include <logic/VerbSequence.h>

#include <cstdint>
#include <span>
#include <vector>

class AuthoredQueryDispatcher;
class GameplayTagRegistry;
class World;

// One run's World and producer: the values its ports hold now, and the
// provenance every command it invokes carries.
struct SequenceRun
{
    const World& Entities;
    VerbDispatcher& Verbs;
    const AuthoredQueryDispatcher* Queries = nullptr;
    const GameplayTagRegistry* Tags = nullptr;
    std::span<const AuthoredValue> Ports;
    VerbInvocationSource Source;
};

struct SequenceOutcome
{
    // Every condition held, so the commands ran.
    bool Held = false;
    std::uint32_t Invoked = 0;
    std::uint32_t Failed = 0;
    // A failed command whose policy is Stop ended the run.
    bool Stopped = false;
    // The first failed command's answer, Accepted when none failed.
    VerbAdmission FirstFailure = VerbAdmission::Accepted;
};

// Owner-thread. Holds scratch so a warmed run allocates nothing for its inputs.
class VerbSequenceRunner
{
public:
    [[nodiscard]] bool Holds(const CompiledVerbSequence& sequence, const SequenceRun& run);

    // Conditions, then every command in authored order. A failed command is one
    // the dispatcher did not accept, one whose value could not be read, or one
    // that never compiled; its policy decides whether the rest still run.
    SequenceOutcome Run(const CompiledVerbSequence& sequence, const SequenceRun& run);

private:
    [[nodiscard]] bool HoldsCondition(const CompiledSequenceCondition& condition, const SequenceRun& run,
                                      std::uint32_t depth);
    [[nodiscard]] bool Read(const CompiledSequenceValue& value, const SequenceRun& run, std::uint32_t depth,
                            AuthoredValue& out);
    [[nodiscard]] bool Ask(const AuthoredQueryHandle& query, std::span<const CompiledSequenceValue> arguments,
                           const SequenceRun& run, std::uint32_t depth, AuthoredValue& answer);

    std::vector<AuthoredValue> Inputs;
    // One argument pack per query nesting depth, kept between runs.
    std::vector<std::vector<AuthoredValue>> QueryArguments;
};
