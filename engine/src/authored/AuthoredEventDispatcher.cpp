#include <authored/AuthoredEventDispatcher.h>

#include <core/logging/Logger.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>

#include <algorithm>
#include <cassert>
#include <string_view>
#include <utility>

namespace
{
    // A subscriber call, or the occurrence itself when nobody listens: every
    // step of a drain costs something, so no chain is free.
    [[nodiscard]] std::size_t CostOf(std::size_t subscribers)
    {
        return subscribers == 0 ? 1 : subscribers;
    }
}

AuthoredEventDispatcher::AuthoredEventDispatcher(const AuthoredEventRegistry& registry, Logger& log)
    : Events(registry)
    , Log(log)
    , Link(std::make_shared<AuthoredEventSubscription::Link>())
{
    Link->Target = this;
    Ring.resize(kDefaultCapacity);
}

AuthoredEventDispatcher::~AuthoredEventDispatcher()
{
    // Subscriptions outliving this become inert rather than dangling.
    Link->Target = nullptr;
}

void AuthoredEventDispatcher::SetCapacity(std::size_t capacity)
{
    if (capacity == 0)
        capacity = 1;
    if (Count != 0 || Draining)
    {
        PendingCapacity = capacity;
        return;
    }
    Ring.clear();
    Ring.resize(capacity);
    Head = 0;
    PendingCapacity = 0;
}

void AuthoredEventDispatcher::ApplyPendingCapacity()
{
    if (PendingCapacity != 0 && Count == 0)
        SetCapacity(PendingCapacity);
}

bool AuthoredEventDispatcher::Enqueue(AuthoredEventId event, EntityId source, InvocationId cause,
                                      const void* published, EncodeFn encode)
{
    if (!Admitting || !Events.IsLive(event))
    {
        ++Refused;
        return false;
    }
    if (Count == Ring.size())
    {
        // Reported once per drain period rather than per refusal, which would
        // bury the one line that says what happened.
        if (Refused == RefusedAtLastDrain)
        {
            Log.Warn("authored events: the queue is full ({} waiting); '{}' was refused",
                     Count, Events.Get(event)->Name);
        }
        ++Refused;
        return false;
    }

    Occurrence& slot = Ring[(Head + Count) % Ring.size()];
    const AuthoredEventSequence sequence{ ++NextSequence };
    const bool duringDelivery = DeliveringSequence.IsValid();
    slot.Event = event;
    slot.Sequence = sequence;
    slot.Root = duringDelivery ? DeliveringRoot : sequence;
    slot.Source = source;
    slot.Cause = AuthoredEventCause{ cause, duringDelivery ? DeliveringSequence : AuthoredEventSequence{} };
    encode(published, slot.Payload);
    ++Count;

    CheckSource(event, source);
    return true;
}

void AuthoredEventDispatcher::CheckSource(AuthoredEventId event, EntityId source)
{
#ifndef NDEBUG
    if (SourceWorld == nullptr || !source.IsValid())
        return;
    const AuthoredEventDefinition* definition = Events.Get(event);
    if (definition == nullptr || definition->SourceComponent.empty())
        return;
    const std::size_t index = AuthoredEventRegistry::IndexOf(event);
    if (index < SourceWarned.size() && SourceWarned[index])
        return;

    const ComponentId component =
        SourceWorld->GetComponentIdByType(MakeComponentTypeId(definition->SourceComponent));
    if (component != InvalidComponentId && SourceWorld->IsAlive(source)
        && SourceWorld->HasComponent(source, component))
    {
        return;
    }
    if (SourceWarned.size() <= index)
        SourceWarned.resize(index + 1, false);
    SourceWarned[index] = true;
    Log.Warn("authored events: '{}' was published by entity {}:{}, which does not carry its "
             "declared source component '{}'. The source is authoring metadata, so the event "
             "was still queued; a graph offering it on that component will not see this one.",
             definition->Name, source.Index, source.Generation, definition->SourceComponent);
#else
    (void)event;
    (void)source;
#endif
}

AuthoredEventSubscription AuthoredEventDispatcher::SubscribeErased(AuthoredEventId event,
                                                                   EntityId source,
                                                                   void* target,
                                                                   DeliverFn deliver)
{
    if (!Events.IsLive(event) || target == nullptr || deliver == nullptr)
        return {};

    const std::size_t index = AuthoredEventRegistry::IndexOf(event);
    if (index >= Subscribers.size())
        Subscribers.resize(index + 1);

    Subscriber subscriber;
    subscriber.Id = AuthoredEventSubscriptionId{ ++NextSubscription };
    subscriber.Generation = AuthoredEventSubscriptionGeneration{ ++NextSubscriptionGeneration };
    subscriber.Source = source;
    subscriber.Target = target;
    subscriber.Deliver = deliver;
    Subscribers[index].push_back(subscriber);
    return AuthoredEventSubscription(Link, subscriber.Id, subscriber.Generation);
}

void AuthoredEventDispatcher::Release(AuthoredEventSubscriptionId id,
                                      AuthoredEventSubscriptionGeneration generation)
{
    for (std::vector<Subscriber>& list : Subscribers)
    {
        for (Subscriber& subscriber : list)
        {
            if (subscriber.Id != id || subscriber.Generation != generation)
                continue;
            // A tombstone while a delivery may be walking the list; removed
            // once nothing is.
            subscriber.Live = false;
            SubscribersDirty = true;
            if (!Draining)
                CompactSubscribers();
            return;
        }
    }
}

void AuthoredEventDispatcher::CompactSubscribers()
{
    for (std::vector<Subscriber>& list : Subscribers)
        std::erase_if(list, [](const Subscriber& subscriber) { return !subscriber.Live; });
    SubscribersDirty = false;
}

std::size_t AuthoredEventDispatcher::MatchingSubscribers(const Occurrence& occurrence) const
{
    const std::size_t index = AuthoredEventRegistry::IndexOf(occurrence.Event);
    if (index >= Subscribers.size())
        return 0;
    std::size_t matching = 0;
    for (const Subscriber& subscriber : Subscribers[index])
    {
        if (subscriber.Live && (!subscriber.Source.IsValid() || subscriber.Source == occurrence.Source))
            ++matching;
    }
    return matching;
}

void AuthoredEventDispatcher::Deliver(const Occurrence& occurrence, std::uint64_t tick)
{
    DeliveringRoot = occurrence.Root;
    DeliveringSequence = occurrence.Sequence;

    TraceRing[TraceNext] = AuthoredEventTraceRecord{
        .Event = occurrence.Event,
        .Sequence = occurrence.Sequence,
        .Root = occurrence.Root,
        .Source = occurrence.Source,
        .Cause = occurrence.Cause,
    };
    TraceNext = (TraceNext + 1) % kTraceDepth;
    TraceSize = std::min(TraceSize + 1, kTraceDepth);

    const std::size_t index = AuthoredEventRegistry::IndexOf(occurrence.Event);
    if (index >= Subscribers.size())
        return;

    const AuthoredEventDelivery delivery{
        .Event = occurrence.Event,
        .Sequence = occurrence.Sequence,
        .Root = occurrence.Root,
        .Source = occurrence.Source,
        .Payload = &occurrence.Payload,
        .Cause = occurrence.Cause,
        .Tick = tick,
    };

    // The count is taken now: a subscription made during this delivery hears
    // the next occurrence, not this one. Each entry is re-read by index
    // because a subscriber may subscribe, which can move the list.
    const std::size_t count = Subscribers[index].size();
    for (std::size_t position = 0; position < count; ++position)
    {
        const Subscriber subscriber = Subscribers[index][position];
        if (!subscriber.Live)
            continue;
        if (subscriber.Source.IsValid() && subscriber.Source != occurrence.Source)
            continue;
        subscriber.Deliver(subscriber.Target, delivery);
    }
}

AuthoredEventDrainResult AuthoredEventDispatcher::Drain(std::uint64_t tick)
{
    AuthoredEventDrainResult result;
    if (Draining)
    {
        Log.Error("authored events: a drain was requested from inside a delivery and refused; "
                  "what a subscriber publishes is delivered by the drain already running");
        return result;
    }
    Draining = true;

    std::size_t spent = 0;
    bool exhausted = false;
    while (Count != 0)
    {
        const Occurrence& occurrence = Ring[Head];
        const std::size_t cost = CostOf(MatchingSubscribers(occurrence));
        // Whole occurrences only, so no subscriber hears one twice. The first
        // is always delivered, so a drain always makes progress.
        if (spent != 0 && spent + cost > Budget)
        {
            exhausted = true;
            break;
        }
        // The slot stays counted while it is delivered, so nothing published
        // during the delivery can be written over it.
        Deliver(occurrence, tick);
        spent += cost;
        Head = (Head + 1) % Ring.size();
        --Count;
        ++result.Delivered;
    }

    DeliveringRoot = {};
    DeliveringSequence = {};
    Draining = false;
    if (SubscribersDirty)
        CompactSubscribers();

    if (exhausted)
        HandleExhaustion(tick, result);
    else
        Suspects.clear();

    result.Remaining = Count;
    RefusedAtLastDrain = Refused;
    ApplyPendingCapacity();
    return result;
}

void AuthoredEventDispatcher::HandleExhaustion(std::uint64_t tick, AuthoredEventDrainResult& result)
{
    result.BudgetExceeded = true;

    // The chains still waiting, in queue order.
    std::vector<AuthoredEventSequence> waiting;
    for (std::size_t offset = 0; offset < Count; ++offset)
    {
        const AuthoredEventSequence root = Ring[(Head + offset) % Ring.size()].Root;
        if (std::ranges::find(waiting, root) == waiting.end())
            waiting.push_back(root);
    }

    Log.Error("authored events: the drain at tick {} spent its budget of {} subscriber calls "
              "with {} occurrences still queued; they wait for the next drain, and a chain "
              "that runs out again is quarantined",
              tick, Budget, Count);
    ReportTrace("most recent deliveries");

    std::vector<AuthoredEventSequence> suspects;
    for (const AuthoredEventSequence root : waiting)
    {
        if (std::ranges::find(Suspects, root) == Suspects.end())
        {
            suspects.push_back(root);
            continue;
        }

        std::size_t discarded = 0;
        Discard(root, discarded);
        Quarantine = AuthoredEventQuarantine{
            .Root = root,
            .Tick = tick,
            .Discarded = discarded,
            .Trace = TraceSnapshot(),
        };
        HasQuarantine = true;
        ++result.QuarantinedRoots;
        Log.Error("authored events: the chain started by occurrence #{} ran out of budget in two "
                  "consecutive drains and is quarantined; {} queued occurrences were discarded. "
                  "A reaction is publishing the event that triggers it, or a provider is "
                  "announcing a change it did not make.",
                  root.Value, discarded);
        assert(!TrapOnQuarantine && "authored event chain quarantined; see the log for its trace");
    }
    Suspects = std::move(suspects);
}

void AuthoredEventDispatcher::Discard(AuthoredEventSequence root, std::size_t& discarded)
{
    // Stable: what remains keeps its order. Swapped rather than assigned so
    // every slot keeps payload storage it has already grown.
    std::size_t kept = 0;
    for (std::size_t offset = 0; offset < Count; ++offset)
    {
        Occurrence& read = Ring[(Head + offset) % Ring.size()];
        if (read.Root == root)
        {
            ++discarded;
            continue;
        }
        if (kept != offset)
            std::swap(Ring[(Head + kept) % Ring.size()], read);
        ++kept;
    }
    Count = kept;
}

std::vector<AuthoredEventTraceRecord> AuthoredEventDispatcher::TraceSnapshot() const
{
    std::vector<AuthoredEventTraceRecord> snapshot;
    snapshot.reserve(TraceSize);
    const std::size_t first = (TraceNext + kTraceDepth - TraceSize) % kTraceDepth;
    for (std::size_t offset = 0; offset < TraceSize; ++offset)
        snapshot.push_back(TraceRing[(first + offset) % kTraceDepth]);
    return snapshot;
}

void AuthoredEventDispatcher::ReportTrace(const char* heading) const
{
    Log.Error("authored events: {}, oldest first:", heading);
    for (const AuthoredEventTraceRecord& record : TraceSnapshot())
    {
        const AuthoredEventDefinition* definition = Events.Get(record.Event);
        const std::string_view name = definition != nullptr ? std::string_view(definition->Name)
                                                            : std::string_view("<retired>");
        Log.Error("  #{} {} from {}:{} (chain #{}, caused by occurrence #{}, request #{})",
                  record.Sequence.Value, name, record.Source.Index, record.Source.Generation,
                  record.Root.Value, record.Cause.Event.Value, record.Cause.Invocation.Value);
    }
}
