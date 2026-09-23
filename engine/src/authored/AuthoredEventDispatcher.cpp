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
    // An occurrence nobody hears still costs one, so no chain is free.
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
    Ring.resize(kDefaultCapacity + 1);
}

AuthoredEventDispatcher::~AuthoredEventDispatcher()
{
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
    Ring.resize(capacity + 1);
    Head = 0;
    PendingCapacity = 0;
}

void AuthoredEventDispatcher::ApplyPendingCapacity()
{
    if (PendingCapacity != 0 && Count == 0)
        SetCapacity(PendingCapacity);
}

bool AuthoredEventDispatcher::ReportOnce(std::vector<bool>& reported, AuthoredEventId event)
{
    const std::size_t index = AuthoredEventRegistry::IndexOf(event);
    if (reported.size() <= index)
        reported.resize(index + 1, false);
    if (reported[index])
        return false;
    reported[index] = true;
    return true;
}

bool AuthoredEventDispatcher::PayloadSatisfies(const AuthoredEventDefinition& definition,
                                               const AuthoredArguments& payload) const
{
    const std::vector<DataFieldSchema>& declared = definition.Payload.Children;
    if (payload.Size() != declared.size())
        return false;
    for (std::size_t index = 0; index < declared.size(); ++index)
    {
        if (!AuthoredValueSatisfiesField(payload.At(index), declared[index]))
            return false;
    }
    return true;
}

bool AuthoredEventDispatcher::Enqueue(AuthoredEventId event, EntityId source, InvocationId cause,
                                      const void* published, EncodeFn encode)
{
    const AuthoredEventDefinition* definition = Admitting ? Events.Get(event) : nullptr;
    if (definition == nullptr)
    {
        ++Refused;
        return false;
    }

    const std::size_t waiting = Delivering ? Count - 1 : Count;
    if (waiting >= Capacity())
    {
        if (!QueueFullReported)
        {
            QueueFullReported = true;
            Log.Warn("authored events: the queue is full ({} waiting); '{}' was refused",
                     waiting, definition->Name);
        }
        ++Refused;
        return false;
    }

    // Encoded in place and claimed only once it satisfies the declaration.
    Occurrence& slot = Ring[(Head + Count) % Ring.size()];
    encode(published, slot.Payload);
    if (!PayloadSatisfies(*definition, slot.Payload))
    {
        if (ReportOnce(InvalidPayloadReported, event))
        {
            Log.Error("authored events: '{}' was published with a payload its declaration does "
                      "not allow, and was refused (wrong kind, unlisted enum choice, or a "
                      "non-finite number).",
                      definition->Name);
        }
        ++Refused;
        return false;
    }

    const AuthoredEventSequence sequence{ ++NextSequence };
    slot.Event = event;
    slot.Contract = Events.Revision(event);
    slot.Sequence = sequence;
    slot.Root = Delivering ? Delivering->Root : sequence;
    slot.Source = source;
    slot.Cause = AuthoredEventCause{ cause, Delivering ? Delivering->Sequence : AuthoredEventSequence{} };
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

    const ComponentId component =
        SourceWorld->GetComponentIdByType(MakeComponentTypeId(definition->SourceComponent));
    const bool carries = component != InvalidComponentId && SourceWorld->IsAlive(source)
                      && SourceWorld->HasComponent(source, component);
    if (carries || !ReportOnce(MissingSourceReported, event))
        return;
    Log.Warn("authored events: '{}' was published by entity {}:{}, which does not carry its "
             "declared source component '{}'. It was still queued; the source is authoring "
             "metadata.",
             definition->Name, source.Index, source.Generation, definition->SourceComponent);
#else
    (void)event;
    (void)source;
#endif
}

AuthoredEventSubscription AuthoredEventDispatcher::SubscribeErased(const AuthoredEventHandle& event,
                                                                   EntityId source,
                                                                   void* target,
                                                                   DeliverFn deliver)
{
    if (!Events.IsCurrent(event) || target == nullptr || deliver == nullptr)
        return {};

    const std::size_t index = AuthoredEventRegistry::IndexOf(event.Slot);
    if (index >= Subscribers.size())
        Subscribers.resize(index + 1);

    Subscriber subscriber;
    subscriber.Serial = ++NextSubscription;
    subscriber.Generation = AuthoredEventSubscriptionGeneration{ ++NextSubscriptionGeneration };
    subscriber.Contract = event.Contract;
    subscriber.Source = source;
    subscriber.Target = target;
    subscriber.Deliver = deliver;
    Subscribers[index].List.push_back(subscriber);
    return AuthoredEventSubscription(
        Link, AuthoredEventSubscriptionKey{ .Event = event.Slot, .Serial = subscriber.Serial },
        subscriber.Generation);
}

void AuthoredEventDispatcher::Release(AuthoredEventSubscriptionKey key,
                                      AuthoredEventSubscriptionGeneration generation)
{
    const std::size_t index = AuthoredEventRegistry::IndexOf(key.Event);
    if (!key.Event.IsValid() || index >= Subscribers.size())
        return;
    SubscriberList& subscribers = Subscribers[index];

    const auto found = std::ranges::lower_bound(subscribers.List, key.Serial, {},
                                                &Subscriber::Serial);
    if (found == subscribers.List.end() || found->Serial != key.Serial
        || found->Generation != generation || !found->Live)
    {
        return;
    }
    found->Live = false;
    ++subscribers.Tombstones;
    if (Draining)
        SubscribersDirty = true;
    else
        CompactIfSparse(subscribers);
}

void AuthoredEventDispatcher::CompactIfSparse(SubscriberList& subscribers)
{
    if (subscribers.Tombstones * 2 <= subscribers.List.size())
        return;
    std::erase_if(subscribers.List, [](const Subscriber& subscriber) { return !subscriber.Live; });
    subscribers.Tombstones = 0;
}

void AuthoredEventDispatcher::CompactSubscribers()
{
    for (SubscriberList& subscribers : Subscribers)
        CompactIfSparse(subscribers);
    SubscribersDirty = false;
}

std::size_t AuthoredEventDispatcher::SubscriptionCount() const
{
    std::size_t live = 0;
    for (const SubscriberList& subscribers : Subscribers)
        live += subscribers.List.size() - subscribers.Tombstones;
    return live;
}

std::size_t AuthoredEventDispatcher::MatchingSubscribers(const Occurrence& occurrence) const
{
    const std::size_t index = AuthoredEventRegistry::IndexOf(occurrence.Event);
    if (index >= Subscribers.size())
        return 0;
    std::size_t matching = 0;
    for (const Subscriber& subscriber : Subscribers[index].List)
    {
        if (subscriber.Live && subscriber.Contract == occurrence.Contract
            && (!subscriber.Source.IsValid() || subscriber.Source == occurrence.Source))
        {
            ++matching;
        }
    }
    return matching;
}

void AuthoredEventDispatcher::Deliver(const Occurrence& occurrence, std::uint64_t tick)
{
    Delivering = InFlight{ .Root = occurrence.Root, .Sequence = occurrence.Sequence };

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

    // Subscriptions made during this delivery hear the next occurrence. Read
    // by index: a subscriber may subscribe and grow the list.
    const std::size_t count = Subscribers[index].List.size();
    for (std::size_t position = 0; position < count; ++position)
    {
        const Subscriber subscriber = Subscribers[index].List[position];
        if (!subscriber.Live || subscriber.Contract != occurrence.Contract)
            continue;
        if (subscriber.Source.IsValid() && subscriber.Source != occurrence.Source)
            continue;
        subscriber.Deliver(subscriber.Target, delivery);
    }
}

namespace
{
    // A subscriber that throws leaves its occurrence at the head for the next
    // drain and the dispatcher neither draining nor delivering.
    template<typename InFlightState>
    class DrainScope
    {
    public:
        DrainScope(bool& draining, InFlightState& delivering)
            : Draining(draining)
            , Delivering(delivering)
        {
            Draining = true;
        }
        ~DrainScope()
        {
            Delivering.reset();
            Draining = false;
        }
        DrainScope(const DrainScope&) = delete;
        DrainScope& operator=(const DrainScope&) = delete;

    private:
        bool& Draining;
        InFlightState& Delivering;
    };
}

AuthoredEventDrainResult AuthoredEventDispatcher::Drain(std::uint64_t tick)
{
    AuthoredEventDrainResult result;
    if (Draining)
    {
        Log.Error("authored events: a drain was requested from inside a delivery and refused");
        return result;
    }

    std::size_t spent = 0;
    bool exhausted = false;
    {
        const DrainScope scope(Draining, Delivering);
        while (Count != 0)
        {
            const Occurrence& occurrence = Ring[Head];
            const std::size_t cost = CostOf(MatchingSubscribers(occurrence));
            if (spent != 0 && spent + cost > Budget)
            {
                exhausted = true;
                break;
            }
            // Popped only after delivery, so nothing published meanwhile
            // overwrites the payload being read.
            Deliver(occurrence, tick);
            spent += cost;
            Head = (Head + 1) % Ring.size();
            --Count;
            ++result.Delivered;
        }
    }

    if (SubscribersDirty)
        CompactSubscribers();

    if (exhausted)
        HandleExhaustion(tick, result);
    else
        Suspects.clear();

    result.Remaining = Count;
    QueueFullReported = false;
    ApplyPendingCapacity();
    return result;
}

void AuthoredEventDispatcher::HandleExhaustion(std::uint64_t tick, AuthoredEventDrainResult& result)
{
    result.BudgetExceeded = true;

    std::vector<AuthoredEventSequence> waitingRoots;
    for (std::size_t offset = 0; offset < Count; ++offset)
    {
        const AuthoredEventSequence root = Ring[(Head + offset) % Ring.size()].Root;
        if (std::ranges::find(waitingRoots, root) == waitingRoots.end())
            waitingRoots.push_back(root);
    }

    Log.Error("authored events: the drain at tick {} spent its budget of {} subscriber calls "
              "with {} occurrences still queued for the next drain",
              tick, Budget, Count);
    ReportTrace("most recent deliveries");

    std::vector<Suspect> suspects;
    for (const AuthoredEventSequence root : waitingRoots)
    {
        const auto previous = std::ranges::find(Suspects, root, &Suspect::Root);
        const std::size_t exhaustions = previous != Suspects.end() ? previous->Exhaustions + 1 : 1;
        if (exhaustions < QuarantineAfter)
        {
            suspects.push_back(Suspect{ .Root = root, .Exhaustions = exhaustions });
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
        Log.Error("authored events: the chain started by occurrence #{} was still running after "
                  "{} exhausted drains and is quarantined; {} queued occurrences were discarded",
                  root.Value, exhaustions, discarded);
        assert(!TrapOnQuarantine && "authored event chain quarantined; see the log for its trace");
    }
    Suspects = std::move(suspects);
}

void AuthoredEventDispatcher::Discard(AuthoredEventSequence root, std::size_t& discarded)
{
    // Stable, and swapped so each slot keeps its grown payload storage.
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
