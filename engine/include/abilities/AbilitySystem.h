#pragma once

#include <time/FrameClock.h>

#include <cstdint>

class AuthoredQueryDispatcher;
class LoggingProvider;
class ConsoleRegistry;
class StoragePartitionSet;
class VerbDispatcher;
class World;
struct FixedLogicContext;

// What an ability's operations reach. A null dispatcher answers every invoke, lease
// or query Unavailable, which the trace records.
struct AbilityHost
{
    VerbDispatcher* Verbs = nullptr;
    AuthoredQueryDispatcher* Queries = nullptr;
    // Registers ability.step_budget and ability.present_linger_ticks; null keeps
    // the defaults.
    ConsoleRegistry* Console = nullptr;
    // Whether this host presents what abilities do: it runs the presenter.
    bool Presents = false;
    // Where a grant whose binding names something this World does not know is
    // reported, once each time the bindings are resolved; null reports nothing.
    LoggingProvider* Logging = nullptr;
};

struct AbilitySettings
{
    // Transitions one record may take in one tick before it ends StepBudget.
    std::uint32_t StepBudget = 8;
    // How long an ended record lingers for presentation.
    std::uint32_t LingerTicks = 8;
};

// The ability pass, once per fixed tick where the simulation is authoritative:
// sweep, input, requests, advance, linger (docs/gameplay/abilitykit.md, "Runtime").
class AbilitySystem
{
public:
    explicit AbilitySystem(AbilityHost host = {});

    void FixedLogic(FixedLogicContext& ctx);

    [[nodiscard]] AbilitySettings& Settings() { return Values; }

private:
    AbilityHost Host;
    AbilitySettings Values;
};

// One pass outside a schedule, for tests and tools; null partitions run every actor.
void RunAbilityPass(World& world, const AbilityHost& host, const FixedSimTime& now,
                    const StoragePartitionSet* partitions = nullptr, const AbilitySettings& settings = {});
