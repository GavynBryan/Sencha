#pragma once

#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredBindingToken.h>
#include <authored/AuthoredEventRegistry.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbId.h>
#include <ecs/EntityId.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

class Logger;
class World;

// The verb request whose operation published an occurrence, and the occurrence
// being delivered at the time. Either may be absent.
struct AuthoredEventCause
{
    InvocationId Invocation;
    AuthoredEventSequence Event;
};

// Payload is valid only for the duration of the call.
struct AuthoredEventDelivery
{
    AuthoredEventId Event;
    AuthoredEventSequence Sequence;
    // The first occurrence of the chain this one belongs to.
    AuthoredEventSequence Root;
    EntityId Source;
    const AuthoredArguments* Payload = nullptr;
    AuthoredEventCause Cause;
    std::uint64_t Tick = 0;
};

struct AuthoredEventTraceRecord
{
    AuthoredEventId Event;
    AuthoredEventSequence Sequence;
    AuthoredEventSequence Root;
    EntityId Source;
    AuthoredEventCause Cause;
};

struct AuthoredEventQuarantine
{
    AuthoredEventSequence Root;
    std::uint64_t Tick = 0;
    std::size_t Discarded = 0;
    // Oldest first.
    std::vector<AuthoredEventTraceRecord> Trace;
};

struct AuthoredEventDrainResult
{
    std::size_t Delivered = 0;
    std::size_t Remaining = 0;
    bool BudgetExceeded = false;
    std::size_t QuarantinedRoots = 0;
};

class AuthoredEventDispatcher;

using AuthoredEventSubscription =
    AuthoredBindingToken<AuthoredEventDispatcher, AuthoredEventSubscriptionKey,
                         AuthoredEventSubscriptionGeneration>;

template<typename E>
concept HasAuthoredEvent = requires(const E& event, AuthoredArguments& payload) {
    AuthoredApiDefinition<E>::EventName;
    AuthoredApiDefinition<E>::Encode(event, payload);
};

// Buffered, per-tick delivery of authored events. Publish only queues; Drain
// delivers FIFO until the queue is empty or the budget is spent. Delivery,
// runaway-chain quarantine and capacity rules are in
// docs/gameplay/authored-api.md. Owner-thread only.
class AuthoredEventDispatcher
{
public:
    static constexpr std::size_t kDefaultBudget = 4096;
    static constexpr std::size_t kDefaultCapacity = 4096;
    static constexpr std::size_t kTraceDepth = 32;

    AuthoredEventDispatcher(const AuthoredEventRegistry& registry, Logger& log);
    ~AuthoredEventDispatcher();

    AuthoredEventDispatcher(const AuthoredEventDispatcher&) = delete;
    AuthoredEventDispatcher& operator=(const AuthoredEventDispatcher&) = delete;
    AuthoredEventDispatcher(AuthoredEventDispatcher&&) = delete;
    AuthoredEventDispatcher& operator=(AuthoredEventDispatcher&&) = delete;

    [[nodiscard]] const AuthoredEventRegistry& Registry() const { return Events; }

    // False when not queued: undeclared, a payload the declaration refuses, a
    // full queue, or closed admission.
    template<HasAuthoredEvent E>
    bool Publish(EntityId source, const E& event, InvocationId cause = {})
    {
        return Enqueue(Resolve<E>(), source, cause, &event,
                       [](const void* published, AuthoredArguments& payload) {
                           AuthoredApiDefinition<E>::Encode(*static_cast<const E*>(published),
                                                            payload);
                       });
    }

    // An invalid source hears every entity. A stale handle subscribes nothing,
    // and a subscription stops hearing the event once its contract moves.
    template<auto Deliver, typename T>
        requires std::is_invocable_v<decltype(Deliver), T&, const AuthoredEventDelivery&>
    [[nodiscard]] AuthoredEventSubscription Subscribe(const AuthoredEventHandle& event,
                                                      EntityId source,
                                                      T& target)
    {
        return SubscribeErased(event, source, &target,
                               [](void* self, const AuthoredEventDelivery& delivery) {
                                   Deliver(*static_cast<T*>(self), delivery);
                               });
    }

    // Refused from inside a delivery.
    AuthoredEventDrainResult Drain(std::uint64_t tick);

    // Subscriber calls per drain. An occurrence is never split across drains.
    void SetBudget(std::size_t budget) { Budget = budget == 0 ? 1 : budget; }
    [[nodiscard]] std::size_t BudgetPerDrain() const { return Budget; }

    // Waiting occurrences, not counting the one being delivered. Applied once
    // the queue is empty.
    void SetCapacity(std::size_t capacity);
    [[nodiscard]] std::size_t Capacity() const { return Ring.size() - 1; }

    // Consecutive exhausted drains before a chain is quarantined.
    void SetQuarantineAfter(std::size_t drains) { QuarantineAfter = drains == 0 ? 1 : drains; }
    [[nodiscard]] std::size_t QuarantineAfterDrains() const { return QuarantineAfter; }

    void SetTrapOnQuarantine(bool trap) { TrapOnQuarantine = trap; }

    // For the development-build source-component check; null disables it.
    void SetWorld(const World* world) { SourceWorld = world; }

    void CloseAdmission() { Admitting = false; }
    [[nodiscard]] bool IsAdmitting() const { return Admitting; }

    [[nodiscard]] bool IsDraining() const { return Draining; }
    [[nodiscard]] std::size_t Queued() const { return Count; }
    [[nodiscard]] std::size_t SubscriptionCount() const;
    [[nodiscard]] std::uint64_t RefusedCount() const { return Refused; }

    [[nodiscard]] const AuthoredEventQuarantine* LastQuarantine() const
    {
        return HasQuarantine ? &Quarantine : nullptr;
    }

private:
    friend AuthoredEventSubscription;

    using EncodeFn = void (*)(const void*, AuthoredArguments&);
    using DeliverFn = void (*)(void*, const AuthoredEventDelivery&);

    struct Occurrence
    {
        AuthoredEventId Event;
        AuthoredEventRevision Contract;
        AuthoredEventSequence Sequence;
        AuthoredEventSequence Root;
        EntityId Source;
        AuthoredEventCause Cause;
        // Storage is reused across publishes.
        AuthoredArguments Payload;
    };

    struct Subscriber
    {
        std::uint64_t Serial = 0;
        AuthoredEventSubscriptionGeneration Generation;
        AuthoredEventRevision Contract;
        EntityId Source;
        void* Target = nullptr;
        DeliverFn Deliver = nullptr;
        bool Live = true;
    };

    // Sorted by Serial. Removed entries stay as tombstones until they
    // outnumber live ones, and never while a drain is walking the list.
    struct SubscriberList
    {
        std::vector<Subscriber> List;
        std::size_t Tombstones = 0;
    };

    struct InFlight
    {
        AuthoredEventSequence Root;
        AuthoredEventSequence Sequence;
    };

    struct Suspect
    {
        AuthoredEventSequence Root;
        std::size_t Exhaustions = 0;
    };

    template<typename E>
    AuthoredEventId Resolve() const
    {
        struct Cache
        {
            AuthoredEventCatalogId Catalog;
            AuthoredEventId Id;
        };
        static Cache cache;
        if (cache.Catalog != Events.Catalog() || !cache.Id.IsValid())
        {
            cache.Catalog = Events.Catalog();
            cache.Id = Events.Find(AuthoredApiDefinition<E>::EventName);
        }
        return cache.Id;
    }

    bool Enqueue(AuthoredEventId event, EntityId source, InvocationId cause,
                 const void* published, EncodeFn encode);
    [[nodiscard]] bool PayloadSatisfies(const AuthoredEventDefinition& definition,
                                        const AuthoredArguments& payload) const;

    [[nodiscard]] AuthoredEventSubscription SubscribeErased(const AuthoredEventHandle& event,
                                                            EntityId source, void* target,
                                                            DeliverFn deliver);
    void Release(AuthoredEventSubscriptionKey key, AuthoredEventSubscriptionGeneration generation);

    void Deliver(const Occurrence& occurrence, std::uint64_t tick);
    [[nodiscard]] std::size_t MatchingSubscribers(const Occurrence& occurrence) const;
    void CompactIfSparse(SubscriberList& subscribers);
    void CompactSubscribers();
    void ApplyPendingCapacity();
    void HandleExhaustion(std::uint64_t tick, AuthoredEventDrainResult& result);
    void Discard(AuthoredEventSequence root, std::size_t& discarded);
    [[nodiscard]] std::vector<AuthoredEventTraceRecord> TraceSnapshot() const;
    void ReportTrace(const char* heading) const;
    void CheckSource(AuthoredEventId event, EntityId source);
    [[nodiscard]] static bool ReportOnce(std::vector<bool>& reported, AuthoredEventId event);

    const AuthoredEventRegistry& Events;
    Logger& Log;
    std::shared_ptr<AuthoredEventSubscription::Link> Link;

    // One slot more than Capacity(), which the occurrence being delivered
    // occupies. Never resized while anything is queued.
    std::vector<Occurrence> Ring;
    std::size_t Head = 0;
    std::size_t Count = 0;
    std::size_t PendingCapacity = 0;

    // Indexed by the event's slot.
    std::vector<SubscriberList> Subscribers;
    std::uint64_t NextSubscription = 0;
    std::uint32_t NextSubscriptionGeneration = 0;
    bool SubscribersDirty = false;

    std::uint64_t NextSequence = 0;
    std::size_t Budget = kDefaultBudget;
    std::size_t QuarantineAfter = 2;
    bool Admitting = true;
    bool Draining = false;
    bool TrapOnQuarantine = false;
    std::uint64_t Refused = 0;
    bool QueueFullReported = false;

    std::optional<InFlight> Delivering;
    std::vector<Suspect> Suspects;

    std::array<AuthoredEventTraceRecord, kTraceDepth> TraceRing{};
    std::size_t TraceNext = 0;
    std::size_t TraceSize = 0;

    AuthoredEventQuarantine Quarantine;
    bool HasQuarantine = false;

    const World* SourceWorld = nullptr;
    std::vector<bool> MissingSourceReported;
    std::vector<bool> InvalidPayloadReported;
};
