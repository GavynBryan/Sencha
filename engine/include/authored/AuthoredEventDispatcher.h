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
#include <type_traits>
#include <vector>

class Logger;
class World;

// What caused a publish: the verb request whose operation announced it, and
// the event whose delivery was in progress when it was announced. Either may
// be absent. What a trace follows to explain a chain.
struct AuthoredEventCause
{
    InvocationId Invocation;
    AuthoredEventSequence Event;
};

// One occurrence, as a subscriber sees it. The payload is valid only for the
// duration of the call; a subscriber that defers work copies what it needs.
struct AuthoredEventDelivery
{
    AuthoredEventId Event;
    AuthoredEventSequence Sequence;
    // The publish that started the chain this occurrence belongs to: itself
    // when it was announced outside a delivery, otherwise inherited from the
    // occurrence being delivered when it was announced.
    AuthoredEventSequence Root;
    EntityId Source;
    const AuthoredArguments* Payload = nullptr;
    AuthoredEventCause Cause;
    std::uint64_t Tick = 0;
};

// One delivered occurrence, as the dispatcher's own trace remembers it.
struct AuthoredEventTraceRecord
{
    AuthoredEventId Event;
    AuthoredEventSequence Sequence;
    AuthoredEventSequence Root;
    EntityId Source;
    AuthoredEventCause Cause;
};

// A chain that kept exhausting the drain budget and was cut off: which one,
// when, how much was thrown away, and what was being delivered when it was
// stopped. Sustained work, not a proven cycle -- a finite chain long enough to
// outlast the threshold is cut off too.
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
    // Still queued, for the next drain, after the budget stopped this one.
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

//=============================================================================
// AuthoredEventDispatcher
//
// Where gameplay code announces that something happened, and where authored
// consumers hear about it.
//
// Publishing only enqueues; it never runs a subscriber. The queue is drained
// once per fixed tick at a single point in the simulation step, first in first
// out, and each occurrence reaches its subscribers in the order they
// subscribed. What a subscriber publishes while being delivered to joins the
// tail of the same drain, so a chain of reactions -- rotate A, A rotated,
// rotate B, B rotated, rotate C -- completes in one tick without any call
// nesting inside another. No verb dispatch is in progress during a drain, so a
// subscriber invoking a verb is an ordinary top-level request.
//
// A drain has a budget of subscriber calls. A drain that runs out stops, keeps
// what is left in order for the next one, and marks the chains still queued as
// suspect. A chain that is still running when the budget runs out on several
// consecutive drains is a runaway -- a reaction feeding the event it reacts
// to, or simply more work than a tick should hold -- and is quarantined: its
// queued occurrences are discarded, and the trace of what it was doing is kept
// for whoever investigates. This is a cutoff for sustained work, not cycle
// detection, and the number of drains it waits is configurable. Other chains
// carry on.
//
// Every occurrence is checked against its declaration when it is published,
// and one whose payload the declaration does not allow is refused. A
// subscriber is bound to the contract revision it resolved: if the event's
// payload changes shape, it stops hearing the event until it resubscribes
// against the new one, rather than reading a layout it was not written for.
//
// A provider that changes state publishes when the state actually changed. A
// request to light a lit torch lights nothing and announces nothing; that
// convention, more than the budget, is what keeps a graph of reactions from
// feeding itself.
//
// Composed by the runtime host. Owner-thread only.
//=============================================================================
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

    // Announces that `event` happened to `source`. False when it was not
    // queued: the event is not declared in this catalog, its payload is not one
    // the declaration allows, the queue is full, or admission is closed.
    template<HasAuthoredEvent E>
    bool Publish(EntityId source, const E& event, InvocationId cause = {})
    {
        return Enqueue(Resolve<E>(), source, cause, &event,
                       [](const void* published, AuthoredArguments& payload) {
                           AuthoredApiDefinition<E>::Encode(*static_cast<const E*>(published),
                                                            payload);
                       });
    }

    // Delivers the event `handle` names to `target` through `Deliver`, for as
    // long as the event's contract is the one the handle was resolved
    // against. With a valid source, only occurrences announced by that entity;
    // with none, every occurrence. A handle from another catalog, or for a
    // contract that has since moved, subscribes nothing. Subscribing during a
    // drain applies from the next occurrence whose delivery has not begun.
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

    // Delivers every queued occurrence, and everything they cause, until the
    // queue is empty or the budget is spent. Refused, and delivers nothing,
    // when called from inside a delivery.
    AuthoredEventDrainResult Drain(std::uint64_t tick);

    // Subscriber calls one drain may make. A single occurrence whose
    // subscribers alone exceed it is still delivered whole, so a drain always
    // makes progress.
    void SetBudget(std::size_t budget) { Budget = budget == 0 ? 1 : budget; }
    [[nodiscard]] std::size_t BudgetPerDrain() const { return Budget; }

    // How many occurrences may wait. The occurrence being delivered is not
    // one of them: a subscriber reacting to it can always queue its reaction
    // while there is room for one waiting occurrence, even if the queue was
    // full when the delivery began. Takes effect at once when nothing is
    // queued, otherwise at the end of the next drain that empties the queue:
    // the queue is never reallocated while a payload is being read.
    void SetCapacity(std::size_t capacity);
    [[nodiscard]] std::size_t Capacity() const { return Ring.size() - 1; }

    // How many consecutive drains a chain may exhaust the budget in before it
    // is quarantined. One cuts off anything that outlasts a single drain.
    void SetQuarantineAfter(std::size_t drains) { QuarantineAfter = drains == 0 ? 1 : drains; }
    [[nodiscard]] std::size_t QuarantineAfterDrains() const { return QuarantineAfter; }

    // Whether a quarantine also stops a debug build at the point it happens.
    void SetTrapOnQuarantine(bool trap) { TrapOnQuarantine = trap; }

    // The World sources are expected to live in, for a development build's
    // check that a source carries the component its event declares. Borrowed;
    // null turns the check off.
    void SetWorld(const World* world) { SourceWorld = world; }

    // Stops accepting publications. The first half of shutdown.
    void CloseAdmission() { Admitting = false; }
    [[nodiscard]] bool IsAdmitting() const { return Admitting; }

    [[nodiscard]] bool IsDraining() const { return Draining; }
    [[nodiscard]] std::size_t Queued() const { return Count; }
    [[nodiscard]] std::size_t SubscriptionCount() const;

    // Publications refused since construction, for any reason.
    [[nodiscard]] std::uint64_t RefusedCount() const { return Refused; }

    // The most recent quarantine, or null if there has been none.
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
        // The contract the payload was encoded against; only subscribers
        // resolved against the same one hear it.
        AuthoredEventRevision Contract;
        AuthoredEventSequence Sequence;
        AuthoredEventSequence Root;
        EntityId Source;
        AuthoredEventCause Cause;
        // Keeps its storage across uses, so a warmed scalar payload is
        // written without allocating.
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

    // One event's subscribers, in subscription order and therefore in serial
    // order. A removed subscriber is left as a tombstone while a delivery may
    // be walking the list, and the list is compacted once tombstones are the
    // larger part of it, so removing many costs amortized constant time each.
    struct SubscriberList
    {
        std::vector<Subscriber> List;
        std::size_t Tombstones = 0;
    };

    // A chain the budget ran out on, and on how many consecutive drains.
    struct Suspect
    {
        AuthoredEventSequence Root;
        std::size_t Exhaustions = 0;
    };

    // Resolved once per event type per catalog and remembered, so publishing
    // never looks a name up after the first time.
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
    void WarnOnce(std::vector<bool>& warned, AuthoredEventId event);

    const AuthoredEventRegistry& Events;
    Logger& Log;
    std::shared_ptr<AuthoredEventSubscription::Link> Link;

    // Fixed-size ring, one slot larger than the capacity so the occurrence
    // being delivered never takes a waiting place: head is the next
    // occurrence to deliver. Never resized while anything is queued.
    std::vector<Occurrence> Ring;
    std::size_t Head = 0;
    std::size_t Count = 0;
    std::size_t PendingCapacity = 0;

    // Indexed by the event's dense slot.
    std::vector<SubscriberList> Subscribers;
    std::uint64_t NextSubscription = 0;
    std::uint32_t NextSubscriptionGeneration = 0;
    bool SubscribersDirty = false;

    std::uint64_t NextSequence = 0;
    std::size_t Budget = kDefaultBudget;
    bool Admitting = true;
    bool Draining = false;
    bool TrapOnQuarantine = false;
    std::size_t QuarantineAfter = 2;
    std::uint64_t Refused = 0;
    // Refused as of the last drain, so a full queue is reported once per
    // drain period rather than once per refusal.
    std::uint64_t RefusedAtLastDrain = 0;

    // The occurrence being delivered, which is what a publish made during its
    // delivery inherits its root and cause from.
    AuthoredEventSequence DeliveringRoot;
    AuthoredEventSequence DeliveringSequence;

    // Chains still queued when the previous drain ran out of budget.
    std::vector<Suspect> Suspects;

    std::array<AuthoredEventTraceRecord, kTraceDepth> TraceRing{};
    std::size_t TraceNext = 0;
    std::size_t TraceSize = 0;

    AuthoredEventQuarantine Quarantine;
    bool HasQuarantine = false;

    const World* SourceWorld = nullptr;
    // Events already reported for a source missing its component, or for a
    // payload their declaration does not allow, so each diagnostic fires once
    // per event rather than once per publish.
    std::vector<bool> SourceWarned;
    std::vector<bool> PayloadWarned;
};
