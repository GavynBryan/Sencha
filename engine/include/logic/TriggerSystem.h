#pragma once

#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <logic/TriggerPrograms.h>
#include <physics/SensorOverlaps.h>

class EngineSchedule;
class ConsoleRegistry;
class Logger;
class LoggingProvider;
struct FixedLogicContext;
struct ZoneResidencyContext;

class TriggerSystem
{
  public:
    TriggerSystem(VerbDispatcher &verbs, const AuthoredQueryDispatcher &queries, const AssetRegistry &assets,
                  const DataAssetCache &dataAssets, Logger &log);
    void FixedLogic(FixedLogicContext &context);
    void ZoneResidency(ZoneResidencyContext &context);
    void Shutdown();
    void SetLimits(std::size_t pairs, std::size_t commands);
    [[nodiscard]] std::size_t PairCapacity() const
    {
        return PairLimit;
    }
    [[nodiscard]] std::size_t CommandBudget() const
    {
        return CommandLimit;
    }
    [[nodiscard]] std::size_t RetainedPairs() const
    {
        return States.size();
    }
    struct Counters
    {
        std::uint64_t Executed = 0;
        std::uint64_t Gated = 0;
        std::uint64_t Failed = 0;
        std::uint64_t Overflowed = 0;
    };
    [[nodiscard]] const Counters &Stats() const
    {
        return Counts;
    }

  private:
    struct State
    {
        SensorPair Pair;
        TriggerProgramHandle Program;
        double NextAllowed = 0;
        double NextStay = 0;
        bool OnceFired = false;
        bool Active = false;
    };
    State *FindState(SensorPair pair, TriggerProgramHandle program);
    void Execute(const SensorOccurrence &occurrence, FixedLogicContext &context, std::size_t &remaining);
    void FillInputs(std::span<const TriggerInput> inputs, const SensorOccurrence &occurrence, const World &world);
    void Failure(const SensorOccurrence &occurrence, VerbAdmission status);
    VerbDispatcher &Verbs;
    const AuthoredQueryDispatcher &Queries;
    const AssetRegistry &Assets;
    const DataAssetCache &DataAssets;
    Logger &Log;
    std::vector<SensorOccurrence> Batch;
    std::vector<State> States;
    std::vector<AuthoredValue> Inputs;
    AuthoredArguments QueryArguments;
    std::vector<std::string> Errors;
    std::size_t PairLimit = 4096;
    std::size_t CommandLimit = 4096;
    std::uint64_t LastSample = 0;
    std::uint64_t LastLogic = 0;
    double Seconds = 0;
    Counters Counts;
};

TriggerSystem &RegisterTriggerSystem(EngineSchedule &schedule, World &world, VerbDispatcher &verbs,
                                     const AuthoredQueryDispatcher &queries, const AssetRegistry &assets,
                                     const DataAssetCache &dataAssets, ConsoleRegistry &console,
                                     LoggingProvider &logging);
