#pragma once

#include <authored/VerbDispatcher.h>
#include <ecs/EntityId.h>
#include <logic/VerbSequenceRunner.h>
#include <physics/SensorOverlaps.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

class AssetRegistry;
class AuthoredQueryDispatcher;
class ConsoleRegistry;
class DataAssetCache;
class EngineSchedule;
class Logger;
class LoggingProvider;
class World;
struct FixedLogicContext;
struct ZoneResidencyContext;

// Activators asking to interact with the Interaction triggers they stand in,
// admitted by trigger.interact and answered at the next trigger pass. A World
// resource so the verb's provider needs nothing but the World.
struct TriggerInteractions
{
    std::vector<EntityId> Activators;
    std::size_t Capacity = 64;
    std::uint64_t Refused = 0;

    [[nodiscard]] bool Admit(EntityId activator)
    {
        if (Activators.size() >= Capacity)
        {
            ++Refused;
            return false;
        }
        Activators.push_back(activator);
        return true;
    }
};

// Turns the physics step's sensor sample into activations at the next
// fixed-logic pass and runs them there, on the authority only, bounded by a
// queue and a command budget. docs/gameplay/triggers.md, "Runtime semantics".
class TriggerSystem
{
public:
    TriggerSystem(VerbDispatcher& verbs,
                  const AuthoredQueryDispatcher* queries,
                  const AssetRegistry* assets,
                  const DataAssetCache* dataAssets,
                  Logger& log);

    void FixedLogic(FixedLogicContext& ctx);
    void ZoneResidency(ZoneResidencyContext& ctx);
    void Shutdown();

    void SetCapacity(std::size_t activations);
    void SetCommandBudget(std::size_t commands);
    [[nodiscard]] std::size_t Capacity() const { return QueueCapacity; }
    [[nodiscard]] std::size_t CommandBudget() const { return Budget; }
    [[nodiscard]] std::size_t Pending() const { return Queue.size(); }
    [[nodiscard]] std::size_t TrackedPairs() const { return Pairs.size(); }

    struct Counters
    {
        std::uint64_t Activations = 0;
        // Held back by a disabled trigger, a cooldown, or a condition.
        std::uint64_t Gated = 0;
        std::uint64_t Invoked = 0;
        std::uint64_t Failed = 0;
        // Refused admission because the queue was full.
        std::uint64_t Refused = 0;
        // Abandoned when run: an entity gone, dormant, or no longer a trigger.
        std::uint64_t Abandoned = 0;
        // Sensor samples the physics step rejected whole for overflowing.
        std::uint64_t SensorOverflows = 0;
    };
    [[nodiscard]] const Counters& Stats() const { return Counts; }

private:
    struct Activation
    {
        EntityId Trigger;
        EntityId Activator;
    };

    // Transient per pair, and per trigger under an invalid activator: when it
    // may activate again, and when a stay next falls due.
    struct PairState
    {
        std::uint64_t ReadyTick = 0;
        std::uint64_t NextStayTick = 0;
        bool Inside = false;
    };

    struct PairHash
    {
        std::size_t operator()(const SensorPair& pair) const
        {
            const EntityIdHash hash;
            return hash(pair.Sensor) * 31u ^ hash(pair.Activator);
        }
    };

    void Admit(EntityId trigger, EntityId activator);
    void Observe(const World& world, const SensorOverlaps& overlaps, double tickSeconds);
    void AnswerInteractions(World& world, const SensorOverlaps* overlaps);
    void RunQueue(FixedLogicContext& ctx);
    void Forget(const World& world, std::uint64_t now);

    VerbDispatcher& Verbs;
    const AuthoredQueryDispatcher* Queries;
    const AssetRegistry* Assets;
    const DataAssetCache* DataAssets;
    Logger& Log;
    VerbSequenceRunner Runner;

    std::vector<Activation> Queue;
    std::vector<Activation> Batch;
    std::vector<EntityId> Consumed;
    std::vector<EntityId> Interactions;
    std::unordered_map<SensorPair, PairState, PairHash> Pairs;
    std::size_t QueueCapacity = 256;
    std::size_t Budget = 1024;
    std::uint64_t LastSampleTick = 0;
    bool SampledBefore = false;
    std::uint64_t ReportedOverflows = 0;
    std::uint64_t ReportedCompiles = 0;
    Counters Counts;
};

// The system, its interaction queue, and the cvars that bound them. Called by
// the host that owns the dispatcher, before content that places a trigger loads.
TriggerSystem& RegisterTriggerSystem(EngineSchedule& schedule,
                                     World& world,
                                     VerbDispatcher& verbs,
                                     const AuthoredQueryDispatcher* queries,
                                     const AssetRegistry* assets,
                                     const DataAssetCache* dataAssets,
                                     ConsoleRegistry& console,
                                     LoggingProvider& logging);
