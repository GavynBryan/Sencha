#pragma once

#include <authored/AuthoredEventDispatcher.h>
#include <authored/AuthoredEventId.h>
#include <authored/AuthoredEventRegistry.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbDispatcher.h>
#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <ecs/World.h>
#include <logic/ResponseSet.h>
#include <logic/VerbSequenceRunner.h>
#include <physics/SensorOverlaps.h>
#include <time/SimClock.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class AssetRegistry;
class AuthoredQueryDispatcher;
class ConsoleRegistry;
class DataAssetCache;
class EngineSchedule;
class Logger;
class LoggingProvider;
struct FixedLogicContext;
struct ZoneResidencyChange;
struct ZoneResidencyContext;

// The occurrences a sensor volume publishes at itself, each carrying the entity
// that crossed it as its activator and instigator.
inline constexpr std::string_view kSensorEnterEvent = "sensor.enter";
inline constexpr std::string_view kSensorStayEvent = "sensor.stay";
inline constexpr std::string_view kSensorExitEvent = "sensor.exit";
inline constexpr std::string_view kSensorInteractEvent = "sensor.interact";

// Activators asking to interact with the volumes they stand in, admitted by
// trigger.interact and answered at the first pass after the tick they asked in.
// A World resource so the verb's provider needs nothing but the World.
struct SensorInteractions
{
    struct Request
    {
        EntityId Activator;
        std::uint64_t Tick = 0;
    };

    std::vector<Request> Requests;
    std::size_t Capacity = 64;
    std::uint64_t Refused = 0;

    [[nodiscard]] bool Admit(EntityId activator, std::uint64_t tick)
    {
        if (Requests.size() >= Capacity)
        {
            ++Refused;
            return false;
        }
        Requests.push_back(Request{ activator, tick });
        return true;
    }
};

// Runs entities' authored responses: an occurrence delivered at an entity is
// matched against that entity's bindings, captured, and run at the first pass of
// a later tick, on the authority only, bounded by a queue and a command budget.
// The physics step's sensor sample is one producer; any native system is
// another, through Deliver. docs/gameplay/responses.md, "Runtime semantics".
class ResponsesSystem
{
public:
    ResponsesSystem(World& world,
                    const FixedSimulationLoop& clock,
                    VerbDispatcher& verbs,
                    const AuthoredQueryDispatcher* queries,
                    const AssetRegistry* assets,
                    const DataAssetCache* dataAssets,
                    Logger& log);

    // Offers `occurrence` to `subject`'s own responses, and to nothing else: it
    // never publishes globally. An entity without Responses costs a validity
    // check and one archetype bit; the payload is encoded only once a binding
    // matches. Returns how many bindings admitted it.
    template<HasAuthoredEvent E>
    std::uint32_t Deliver(EntityId subject, const E& occurrence)
    {
        if (!HoldsResponses(subject))
            return 0;
        return Admit(subject, EventOf<E>(), Clock.GetTickIndex(), &occurrence,
                     [](const void* published, AuthoredArguments& payload) {
                         AuthoredApiDefinition<E>::Encode(*static_cast<const E*>(published), payload);
                     });
    }

    [[nodiscard]] bool HoldsResponses(EntityId subject) const
    {
        return subject.IsValid() && ResponsesComponent != InvalidComponentId
            && Entities.HasComponent(subject, ResponsesComponent);
    }

    void FixedLogic(FixedLogicContext& ctx);
    void ZoneResidency(ZoneResidencyContext& ctx);
    void Shutdown();

    void SetCapacity(std::size_t activations);
    void SetCommandBudget(std::size_t commands);
    [[nodiscard]] std::size_t Capacity() const { return QueueCapacity; }
    [[nodiscard]] std::size_t CommandBudget() const { return Budget; }
    [[nodiscard]] std::size_t Pending() const { return Queue.size(); }
    [[nodiscard]] std::size_t TrackedCooldowns() const { return Cooldowns.size(); }
    // Bytes held for waiting activations: entries and their captured payloads.
    [[nodiscard]] std::size_t QueueBytes() const;

    struct Counters
    {
        // Deliveries that reached an entity carrying Responses.
        std::uint64_t Delivered = 0;
        // Bindings that admitted an occurrence into the queue.
        std::uint64_t Admitted = 0;
        // Payloads encoded: at most one per delivery, and only on a match.
        std::uint64_t Encoded = 0;
        // Held back by a disabled or consumed binding, a cooldown, or a condition.
        std::uint64_t Gated = 0;
        // Conditions held and the commands ran.
        std::uint64_t Activations = 0;
        std::uint64_t Invoked = 0;
        std::uint64_t Failed = 0;
        // Refused admission because the queue was full.
        std::uint64_t Refused = 0;
        // Dropped before running: the binding or its schema changed, or the
        // recipient's zone went dormant or unloaded.
        std::uint64_t Abandoned = 0;
        // Payloads their event's declaration does not allow.
        std::uint64_t InvalidPayloads = 0;
        // Sensor samples the physics step rejected whole for overflowing.
        std::uint64_t SensorOverflows = 0;
    };
    [[nodiscard]] const Counters& Stats() const { return Counts; }

private:
    using EncodeFn = void (*)(const void*, AuthoredArguments&);

    // An admitted response, with everything it needs captured: a recipient that
    // ends before the pass still runs it, and nothing here points into ECS storage.
    struct Activation
    {
        EntityId Recipient;
        EntityId Instigator;
        StoragePartitionId Partition;
        ResponseSetKey Set{};
        VerbSequenceKey Sequence{};
        std::uint64_t KeyHash = 0;
        std::uint64_t Tick = 0;
        AuthoredEventId Event{};
        AuthoredEventRevision Revision{};
        std::uint32_t Values = 0;
        std::uint16_t ValueCount = 0;
        std::uint8_t Binding = 0;
        bool Once = false;
        ResponseCooldownScope Scope = ResponseCooldownScope::Binding;
        float CooldownSeconds = 0.0f;
    };

    struct CooldownKey
    {
        EntityId Recipient;
        std::uint64_t KeyHash = 0;
        EntityId Instigator;
        friend bool operator==(const CooldownKey&, const CooldownKey&) = default;
    };

    struct CooldownHash
    {
        std::size_t operator()(const CooldownKey& key) const
        {
            const EntityIdHash hash;
            return (hash(key.Recipient) * 31u ^ static_cast<std::size_t>(key.KeyHash)) * 31u ^ hash(key.Instigator);
        }
    };

    template<typename E>
    AuthoredEventId EventOf()
    {
        const AuthoredEventRegistry* events = EventCatalog();
        if (events == nullptr)
            return {};
        struct Cache
        {
            AuthoredEventCatalogId Catalog;
            AuthoredEventId Id;
        };
        static Cache cache;
        if (cache.Catalog != events->Catalog() || !cache.Id.IsValid())
        {
            cache.Catalog = events->Catalog();
            cache.Id = events->Find(AuthoredApiDefinition<E>::EventName);
        }
        return cache.Id;
    }

    [[nodiscard]] const AuthoredEventRegistry* EventCatalog();
    std::uint32_t Admit(EntityId subject, AuthoredEventId event, std::uint64_t tick, const void* occurrence,
                        EncodeFn encode);
    bool Encode(EntityId subject, AuthoredEventId event, const void* occurrence, EncodeFn encode,
                std::uint32_t& values, std::uint16_t& count);
    [[nodiscard]] bool Cooling(EntityId recipient, std::uint64_t keyHash, ResponseCooldownScope scope,
                               EntityId instigator, std::uint64_t tick) const;
    void ResolveSensorEvents();
    void Observe(const SensorOverlaps& overlaps);
    void AnswerInteractions(World& world, const SensorOverlaps* overlaps, std::uint64_t now);
    void RunQueue(FixedLogicContext& ctx);
    void Compact(std::size_t waiting);
    void CaptureZoneState(World& world, const ZoneResidencyChange& change) const;
    void RestoreZoneState(World& world, const ZoneResidencyChange& change) const;

    World& Entities;
    const FixedSimulationLoop& Clock;
    VerbDispatcher& Verbs;
    const AuthoredQueryDispatcher* Queries;
    const AssetRegistry* Assets;
    const DataAssetCache* DataAssets;
    Logger& Log;
    VerbSequenceRunner Runner;
    ComponentId ResponsesComponent = InvalidComponentId;
    const AuthoredEventRegistry* Events = nullptr;

    // Enter, stay, exit, interact.
    std::array<AuthoredEventId, 4> SensorEvents{};
    CatalogStamp SensorEventsResolved;

    std::vector<Activation> Queue;
    // Captured ports: the recipient, then the payload, per activation.
    std::vector<AuthoredValue> Values;
    std::vector<std::size_t> Carried;
    std::vector<std::pair<EntityId, std::uint64_t>> ConsumedThisPass;
    std::vector<AuthoredValue> RunPorts;
    AuthoredArguments Scratch;
    std::unordered_map<CooldownKey, std::uint64_t, CooldownHash> Cooldowns;
    std::size_t QueueCapacity = 256;
    std::size_t Budget = 1024;
    std::uint64_t LastSampleTick = 0;
    bool SampledBefore = false;
    std::uint64_t ReportedOverflows = 0;
    std::uint64_t ReportedCompiles = 0;
    bool QueueFullReported = false;
    std::vector<bool> InvalidPayloadReported;
    Counters Counts;
};

// The system, its interaction queue, and the cvars that bound them. Called by
// the host that owns the dispatcher, before content that places responses loads.
ResponsesSystem& RegisterResponsesSystem(EngineSchedule& schedule,
                                         World& world,
                                         const FixedSimulationLoop& clock,
                                         VerbDispatcher& verbs,
                                         const AuthoredQueryDispatcher* queries,
                                         const AssetRegistry* assets,
                                         const DataAssetCache* dataAssets,
                                         ConsoleRegistry& console,
                                         LoggingProvider& logging);
