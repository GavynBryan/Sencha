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
    Ring.resize(kDefaultCapacity + 1);
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
    Ring.resize(capacity + 1);
    Head = 0;
    PendingCapacity = 0;
}

void AuthoredEventDispatcher::ApplyPendingCapacity()
{
    if (PendingCapacity != 0 && Count == 0)
        SetCapacity(PendingCapacity);
}

void AuthoredEventDispatcher::WarnOnce(std::vector<bool>& warned, AuthoredEventId event)
{
    const std::size_t index = AuthoredEventRegistry::IndexOf(event);
    if (warned.size() <= index)
        warned.resize(index + 1, false);
    warned[index] = true;
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

    // The occurrence being delivered holds a slot of its own, so it never
    // stands between a subscriber and the reaction it queues.
    const bool duringDelivery = DeliveringSequence.IsValid();
    const std::size_t waiting = duringDelivery ? Count - 1 : Count;
    if (waiting >= Capacity())
    {
        // Reported once per drain period rather than per refusal, which would
        // bury the one line that says what happened.
        if (Refused == RefusedAtLastDrain)
        {
            Log.Warn("authored events: the queue is full ({} waiting); '{}' was refused",
                     waiting, definition->Name);
        }
        ++Refused;
        return false;
    }

    Occurrence& slot = Ring[(Head + Count) % Ring.size()];
    encode(published, slot.Payload);
    // Checked where it is announced, so no subscriber is ever handed a payload
    // its declaration does not allow -- an enumerator the schema does not list,
    // a number that is not finite. The slot is simply not claimed.
    if (!PayloadSatisfies(*definition, slot.Payload))
    {
        const std::size_t index = AuthoredEventRegistry::IndexOf(event);
        if (index >= PayloadWarned.size() || !PayloadWarned[index])
        {
            WarnOnce(PayloadWarned, event);
            Log.Error("authored events: '{}' was published with a payload its declaration does "
                      "not allow, and was refused. The encoder produced a value of the wrong "
                      "kind, an unlisted enum choice, or a number that is not finite.",
                      definition->Name);
        }
        ++Refused;
        return false;
    }

    const AuthoredEventSequence sequence{ ++NextSequence };
    slot.Event = event;
    slot.Contract = Events.Revision(event);
    slot.Sequence = sequence;
    slot.Root = duringDelivery ? DeliveringRoot : sequence;
    slot.Source = source;
    slot.Cause = AuthoredEventCause{ cause, duringDelivery ? DeliveringSequence : AuthoredEventSequence{} };
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
    WarnOnce(SourceWarned, event);
    Log.Warn("authored events: '{}' was published by entity {}:{}, which does not carry its "
             "declared source component '{}'. The source is authoring metadata, so the event "
             "was still queued; a graph offering it on that component will not see this one.",
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

    // The list is in serial order, compaction included, so the subscription is
    // found by search rather than by walking the dispatcher.
    const auto found = std::ranges::lower_bound(subscribers.List, key.Serial, {},
                                                &Subscriber::Serial);
    if (found == subscribers.List.end() || found->Serial != key.Serial
        || found->Generation != generation || !found->Live)
    {
        return;
    }
    found->Live = false;
    ++subscribers.Tombstones;
    // Left as a tombstone while a delivery may be walking the list.
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
    // because a subscriber may subscribe, which can move the list; nothing is
    // removed from it while a drain runs.
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
    // Ends a drain however it ends. A subscriber that throws leaves the
    // occurrence it was being delivered at the head of the queue, to be
    // delivered again by the next drain, and leaves the dispatcher usable:
    // not still draining, and not still delivering.
    class DrainScope
    {
    public:
        DrainScope(bool& draining, AuthoredEventSequence& root, AuthoredEventSequence& sequence)
            : Draining(draining)
            , Root(root)
            , Sequence(sequence)
        {
            Draining = true;
        }
        ~DrainScope()
        {
            Root = {};
            Sequence = {};
            Draining = false;
        }
        DrainScope(const DrainScope&) = delete;
        DrainScope& operator=(const DrainScope&) = delete;

    private:
        bool& Draining;
        AuthoredEventSequence& Root;
        AuthoredEventSequence& Sequence;
    };
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

    std::size_t spent = 0;
    bool exhausted = false;
    {
        const DrainScope scope(Draining, DeliveringRoot, DeliveringSequence);
        while (Count != 0)
        {
            const Occurrence& occurrence = Ring[Head];
            const std::size_t cost = CostOf(MatchingSubscribers(occurrence));
            // Whole occurrences only, so no subscriber hears one twice. The
            // first is always delivered, so a drain always makes progress.
            if (spent != 0 && spent + cost > Budget)
            {
                exhausted = true;
                break;
            }
            // The slot stays counted while it is delivered, so nothing
            // published during the delivery can be written over it.
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
              "still running after {} consecutive exhausted drains is quarantined",
              tick, Budget, Count, QuarantineAfter);
    ReportTrace("most recent deliveries");

    std::vector<Suspect> suspects;
    for (const AuthoredEventSequence root : waiting)
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
                  "{} consecutive exhausted drains and is quarantined; {} queued occurrences "
                  "were discarded. A reaction may be publishing the event that triggers it, a "
                  "provider may be announcing a change it did not make, or the chain is simply "
                  "more work than the budget allows.",
                  root.Value, exhaustions, discarded);
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
