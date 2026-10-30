#include <logic/ResponsesSystem.h>

#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <authored/WorldVocabulary.h>
#include <core/console/ConsoleRegistry.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/Query.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/Responses.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/identity/PersistentIdComponent.h>
#include <zone/ZoneStateStore.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace
{
std::uint64_t TicksIn(float seconds, double tickSeconds)
{
    if (!(seconds > 0.0f) || !(tickSeconds > 0.0))
        return 0;
    // Authored seconds are floats: 0.3 s of 0.1 s ticks is three, not the four
    // its rounding error would ceil to.
    const double ticks = static_cast<double>(seconds) / tickSeconds;
    const double nearest = std::round(ticks);
    return static_cast<std::uint64_t>(std::abs(ticks - nearest) < 1.0e-3 ? nearest : std::ceil(ticks));
}

struct SensorActivation
{
    EntityId Activator;
};

void EncodeSensorActivation(const void* published, AuthoredArguments& payload)
{
    payload.Resize(1);
    payload.Set(0, AuthoredValue::Entity(static_cast<const SensorActivation*>(published)->Activator));
}

bool SwitchedOff(const Responses& responses, std::uint32_t bit)
{
    return ((responses.Disabled | responses.Consumed) & bit) != 0;
}

bool SatisfiesDeclaration(const AuthoredEventDefinition& definition, const AuthoredArguments& payload)
{
    const std::vector<DataFieldSchema>& declared = definition.Payload.Children;
    bool valid = payload.Size() == declared.size();
    for (std::size_t i = 0; valid && i < declared.size(); ++i)
        valid = AuthoredValueSatisfiesField(payload.At(i), declared[i]);
    return valid;
}

// What a zone remembers of one binding: whose state, the text it had, and the
// two bits that differ from authored.
struct RememberedBinding
{
    std::uint64_t KeyHash = 0;
    std::uint64_t DefinitionHash = 0;
    std::uint8_t Disabled = 0;
    std::uint8_t Consumed = 0;
    std::uint8_t Padding[6] = {};
};
static_assert(std::is_trivially_copyable_v<RememberedBinding>);

const CompiledVerbSequence kEmptySequence{};
} // namespace

ResponsesSystem::ResponsesSystem(World& world,
                                 const FixedSimulationLoop& clock,
                                 VerbDispatcher& verbs,
                                 const AuthoredQueryDispatcher* queries,
                                 const AssetRegistry* assets,
                                 const DataAssetCache* dataAssets,
                                 Logger& log)
    : Entities(world)
    , Clock(clock)
    , Verbs(verbs)
    , Queries(queries)
    , Assets(assets)
    , DataAssets(dataAssets)
    , Log(log)
{
    if (world.IsRegistered<Responses>())
        ResponsesComponent = world.GetComponentId<Responses>();
}

void ResponsesSystem::SetCapacity(std::size_t activations)
{
    QueueCapacity = std::max<std::size_t>(activations, 1);
}

void ResponsesSystem::SetCommandBudget(std::size_t commands)
{
    Budget = std::max<std::size_t>(commands, 1);
}

std::size_t ResponsesSystem::QueueBytes() const
{
    return Queue.size() * sizeof(Activation) + Values.size() * sizeof(AuthoredValue);
}

const AuthoredEventRegistry* ResponsesSystem::EventCatalog()
{
    if (Events == nullptr)
        Events = FindAuthoredEventRegistry(std::as_const(Entities));
    return Events;
}

bool ResponsesSystem::Encode(EntityId subject, AuthoredEventId event, const void* occurrence, EncodeFn encode,
                             std::uint32_t& values, std::uint16_t& count)
{
    const AuthoredEventDefinition* definition = Events != nullptr ? Events->Get(event) : nullptr;
    if (definition == nullptr)
        return false;
    encode(occurrence, Scratch);
    if (!SatisfiesDeclaration(*definition, Scratch))
    {
        ++Counts.InvalidPayloads;
        const std::size_t index = AuthoredEventRegistry::IndexOf(event);
        if (InvalidPayloadReported.size() <= index)
            InvalidPayloadReported.resize(index + 1, false);
        if (!InvalidPayloadReported[index])
        {
            InvalidPayloadReported[index] = true;
            Log.Error("responses: '{}' was delivered with a payload its declaration does not allow, and was "
                      "refused (wrong kind, unlisted enum choice, or a non-finite number)",
                      definition->Name);
        }
        return false;
    }

    values = static_cast<std::uint32_t>(Values.size());
    count = static_cast<std::uint16_t>(kResponsePorts.size() + Scratch.Size());
    Values.push_back(AuthoredValue::Entity(subject));
    for (const AuthoredValue& value : Scratch.Values())
        Values.push_back(value);
    ++Counts.Encoded;
    return true;
}

bool ResponsesSystem::Cooling(EntityId recipient, std::uint64_t keyHash, ResponseCooldownScope scope,
                              EntityId instigator, std::uint64_t tick) const
{
    if (Cooldowns.empty())
        return false;
    const CooldownKey key{ recipient, keyHash,
                           scope == ResponseCooldownScope::Instigator ? instigator : EntityId{} };
    const auto found = Cooldowns.find(key);
    return found != Cooldowns.end() && found->second > tick;
}

const CompiledVerbSequence* ResponsesSystem::ResolveSequence(ResponseDefinitions& definitions, VerbSequenceKey sequence)
{
    const CompiledVerbSequence* compiled = definitions.Sequences.Resolve(sequence, Entities, Assets, DataAssets);
    if (definitions.Sequences.CompileCount() != ReportedCompiles)
    {
        ReportedCompiles = definitions.Sequences.CompileCount();
        for (const std::string& diagnostic : definitions.Sequences.Diagnostics(sequence))
            Log.Error("response sequence: {}", diagnostic);
    }
    return compiled;
}

ResponseAvailability ResponsesSystem::Judge(EntityId subject, AuthoredEventId event, const void* occurrence,
                                            EncodeFn encode)
{
    ResponseAvailability answer{ .RuntimeStateKnown = IsSimulationAuthority(Entities) };
    if (!event.IsValid() || EventCatalog() == nullptr || !HoldsResponses(subject))
        return answer;
    const auto* responses =
        static_cast<const Responses*>(std::as_const(Entities).GetComponentRaw(subject, ResponsesComponent));
    ResponseDefinitions* definitions = Entities.TryGetResource<ResponseDefinitions>();
    const AuthoredEventDefinition* declared = Events->Get(event);
    if (responses == nullptr || definitions == nullptr || declared == nullptr)
        return answer;
    const ResponseSetView set = definitions->Sets.Resolve(responses->Set, Entities);
    const std::span<const CompiledResponseBinding> compiled = set.Compiled;
    const std::span<const ResponseBindingDesc> bindings = set.Bindings;

    const std::uint64_t now = Clock.GetTickIndex();
    bool encoded = false;
    EntityId instigator;
    for (std::size_t i = 0; i < compiled.size(); ++i)
    {
        if (compiled[i].Event != event)
            continue;
        answer.Readiness = ResponseReadiness::Blocked;
        if (answer.RuntimeStateKnown && SwitchedOff(*responses, 1u << i))
            continue;
        if (!encoded)
        {
            encode(occurrence, JudgeScratch);
            if (!SatisfiesDeclaration(*declared, JudgeScratch))
                return answer;
            JudgePorts.clear();
            JudgePorts.push_back(AuthoredValue::Entity(subject));
            for (const AuthoredValue& value : JudgeScratch.Values())
                JudgePorts.push_back(value);
            encoded = true;
            if (compiled[i].InstigatorPort < JudgePorts.size())
                (void)JudgePorts[compiled[i].InstigatorPort].TryGetEntity(instigator);
        }
        const ResponseBindingDesc& binding = bindings[i];
        if (answer.RuntimeStateKnown && Cooling(subject, binding.KeyHash, binding.CooldownScope, instigator, now))
            continue;
        const CompiledVerbSequence* sequence = ResolveSequence(*definitions, binding.Sequence);
        const SequenceRun run{
            .Entities = Entities,
            .Verbs = Verbs,
            .Queries = Queries,
            .Tags = Entities.TryGetResource<GameplayTagRegistry>(),
            .Ports = JudgePorts,
            .Source = VerbInvocationSource{ .Producer = subject, .Instigator = instigator, .Tick = now },
        };
        if (JudgeRunner.Holds(sequence != nullptr ? *sequence : kEmptySequence, run))
        {
            answer.Readiness = ResponseReadiness::Available;
            return answer;
        }
    }
    return answer;
}

std::uint32_t ResponsesSystem::Admit(EntityId subject, AuthoredEventId event, std::uint64_t tick,
                                     const void* occurrence, EncodeFn encode)
{
    ++Counts.Delivered;
    if (!event.IsValid() || EventCatalog() == nullptr || !IsSimulationAuthority(Entities))
        return 0;
    const auto* responses =
        static_cast<const Responses*>(std::as_const(Entities).GetComponentRaw(subject, ResponsesComponent));
    ResponseDefinitions* definitions = Entities.TryGetResource<ResponseDefinitions>();
    if (responses == nullptr || definitions == nullptr)
        return 0;
    const ResponseSetView set = definitions->Sets.Resolve(responses->Set, Entities);
    const std::span<const CompiledResponseBinding> compiled = set.Compiled;
    const std::span<const ResponseBindingDesc> bindings = set.Bindings;

    std::uint32_t admitted = 0;
    bool encoded = false;
    std::uint32_t values = 0;
    std::uint16_t count = 0;
    EntityId instigator;
    for (std::size_t i = 0; i < compiled.size(); ++i)
    {
        if (compiled[i].Event != event)
            continue;
        const std::uint32_t bit = 1u << i;
        if (SwitchedOff(*responses, bit))
        {
            ++Counts.Gated;
            continue;
        }
        if (!encoded)
        {
            if (!Encode(subject, event, occurrence, encode, values, count))
                return admitted;
            encoded = true;
            if (compiled[i].InstigatorPort < count)
                (void)Values[values + compiled[i].InstigatorPort].TryGetEntity(instigator);
        }
        const ResponseBindingDesc& binding = bindings[i];
        // Cooling is judged now, whenever the occurrence happened.
        if (Cooling(subject, binding.KeyHash, binding.CooldownScope, instigator, Clock.GetTickIndex()))
        {
            ++Counts.Gated;
            continue;
        }
        if (Queue.size() >= QueueCapacity)
        {
            ++Counts.Refused;
            if (!QueueFullReported)
            {
                QueueFullReported = true;
                Log.Warn("responses: activation queue full ({}); raise logic.responses.queue_capacity",
                         QueueCapacity);
            }
            continue;
        }
        Queue.push_back(Activation{
            .Recipient = subject,
            .Instigator = instigator,
            .Partition = Entities.GetEntityPartition(subject),
            .Set = responses->Set,
            .Sequence = binding.Sequence,
            .KeyHash = binding.KeyHash,
            .Tick = tick,
            .Event = event,
            .Revision = compiled[i].Revision,
            .Values = values,
            .ValueCount = count,
            .Binding = static_cast<std::uint8_t>(i),
            .Once = binding.Once,
            .Scope = binding.CooldownScope,
            .CooldownSeconds = binding.CooldownSeconds,
        });
        ++admitted;
        ++Counts.Admitted;
    }
    return admitted;
}

void ResponsesSystem::ResolveSensorEvents()
{
    const AuthoredEventRegistry* events = EventCatalog();
    if (events == nullptr)
        return;
    const CatalogStamp now = CatalogStamp::Of(*events);
    if (now == SensorEventsResolved)
        return;
    SensorEventsResolved = now;
    SensorEvents = { events->Find(kSensorEnterEvent), events->Find(kSensorStayEvent),
                     events->Find(kSensorExitEvent), events->Find(kSensorInteractEvent) };
}

void ResponsesSystem::Observe(const SensorOverlaps& overlaps)
{
    if (overlaps.OverflowCount() != ReportedOverflows)
    {
        Counts.SensorOverflows += overlaps.OverflowCount() - ReportedOverflows;
        ReportedOverflows = overlaps.OverflowCount();
        Log.Warn("responses: a sensor sample overflowed and was dropped; crossings in it are lost");
    }

    const std::span<const SensorOccurrence> occurrences = overlaps.Occurrences();
    if (occurrences.empty() || (SampledBefore && occurrences.front().Tick <= LastSampleTick))
        return;
    LastSampleTick = occurrences.front().Tick;
    SampledBefore = true;

    for (const SensorOccurrence& occurrence : occurrences)
    {
        const EntityId sensor = occurrence.Pair.Sensor;
        if (!HoldsResponses(sensor))
            continue;
        const SensorActivation activation{ occurrence.Pair.Activator };
        // Staying begins with entering: an entry sample is the first sample inside.
        if (occurrence.Transition == SensorTransition::Enter)
            (void)Admit(sensor, SensorEvents[0], occurrence.Tick, &activation, &EncodeSensorActivation);
        if (occurrence.Transition != SensorTransition::Exit)
            (void)Admit(sensor, SensorEvents[1], occurrence.Tick, &activation, &EncodeSensorActivation);
        else
            (void)Admit(sensor, SensorEvents[2], occurrence.Tick, &activation, &EncodeSensorActivation);
    }
}

void ResponsesSystem::AnswerInteractions(World& world, const SensorOverlaps* overlaps, std::uint64_t now)
{
    SensorInteractions* requests = world.TryGetResource<SensorInteractions>();
    if (requests == nullptr || requests->Requests.empty())
        return;
    // A request is answered at the first pass of a later tick than the one it
    // was made in, as any delivery is.
    std::size_t kept = 0;
    for (const SensorInteractions::Request& request : requests->Requests)
    {
        if (request.Tick >= now)
        {
            requests->Requests[kept++] = request;
            continue;
        }
        if (overlaps == nullptr)
            continue;
        const SensorActivation activation{ request.Activator };
        for (const SensorPair& pair : overlaps->Pairs())
            if (pair.Activator == request.Activator && HoldsResponses(pair.Sensor))
                (void)Admit(pair.Sensor, SensorEvents[3], request.Tick, &activation, &EncodeSensorActivation);
    }
    requests->Requests.resize(kept);
}

void ResponsesSystem::RunQueue(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    ResponseDefinitions* definitions = world.TryGetResource<ResponseDefinitions>();
    const std::uint64_t now = ctx.Time.TickIndex;
    const std::size_t waiting = Queue.size();
    Carried.clear();
    ConsumedThisPass.clear();
    std::size_t spent = 0;

    for (std::size_t index = 0; index < waiting; ++index)
    {
        // Copied: a command may deliver, which grows the queue it lives in.
        const Activation activation = Queue[index];
        if (activation.Tick >= now || spent >= Budget)
        {
            Carried.push_back(index);
            continue;
        }
        const std::pair<EntityId, std::uint64_t> binding{ activation.Recipient, activation.KeyHash };
        if (std::ranges::find(ConsumedThisPass, binding) != ConsumedThisPass.end())
        {
            ++Counts.Gated;
            continue;
        }
        if (definitions == nullptr || Events == nullptr || Events->Revision(activation.Event) != activation.Revision)
        {
            ++Counts.Abandoned;
            continue;
        }

        // A recipient that still exists answers from its live state; one that
        // ended runs what it captured, unless its zone is gone.
        const bool alive = world.IsAlive(activation.Recipient);
        const std::uint32_t bit = 1u << activation.Binding;
        if (alive)
        {
            const auto* responses = static_cast<const Responses*>(
                std::as_const(world).GetComponentRaw(activation.Recipient, ResponsesComponent));
            if (!ctx.Partitions.Contains(world.GetEntityPartition(activation.Recipient)) || responses == nullptr
                || responses->Set != activation.Set)
            {
                ++Counts.Abandoned;
                continue;
            }
            if (SwitchedOff(*responses, bit))
            {
                ++Counts.Gated;
                continue;
            }
        }
        else if (!ctx.Partitions.Contains(activation.Partition))
        {
            ++Counts.Abandoned;
            continue;
        }
        if (Cooling(activation.Recipient, activation.KeyHash, activation.Scope, activation.Instigator, now))
        {
            ++Counts.Gated;
            continue;
        }

        const CompiledVerbSequence* sequence = ResolveSequence(*definitions, activation.Sequence);
        if (sequence == nullptr)
            sequence = &kEmptySequence;

        RunPorts.assign(Values.begin() + activation.Values,
                        Values.begin() + activation.Values + activation.ValueCount);
        const SequenceRun run{
            .Entities = world,
            .Verbs = Verbs,
            .Queries = Queries,
            .Tags = world.TryGetResource<GameplayTagRegistry>(),
            .Ports = RunPorts,
            .Source = VerbInvocationSource{ .Producer = activation.Recipient,
                                            .Instigator = activation.Instigator,
                                            .Tick = now },
        };
        const SequenceOutcome outcome = Runner.Run(*sequence, run);
        spent += std::max<std::size_t>(1, sequence->Commands.size());

        if (!outcome.Held)
        {
            ++Counts.Gated;
            continue;
        }
        ++Counts.Activations;
        Counts.Invoked += outcome.Invoked;
        Counts.Failed += outcome.Failed;
        if (const std::uint64_t cooldown = TicksIn(activation.CooldownSeconds, ctx.Time.DeltaSeconds); cooldown > 0)
        {
            const CooldownKey key{ activation.Recipient, activation.KeyHash,
                                   activation.Scope == ResponseCooldownScope::Instigator ? activation.Instigator
                                                                                         : EntityId{} };
            Cooldowns[key] = now + cooldown;
        }
        if (activation.Once)
        {
            // Looked up again: a command may have moved the row or ended it.
            if (auto* responses = static_cast<Responses*>(world.GetComponentRaw(activation.Recipient, ResponsesComponent));
                responses != nullptr && responses->Set == activation.Set)
                responses->Consumed |= bit;
            ConsumedThisPass.push_back(binding);
        }
    }
    Compact(waiting);
}

// What waits keeps its order, ahead of what this pass admitted; payloads move
// with their activations, and a once-binding that ran takes the rest of its
// waiting occurrences with it. Compacted in place: what is kept only ever moves
// toward the front, so one warmed buffer serves every pass.
void ResponsesSystem::Compact(std::size_t waiting)
{
    std::size_t entries = 0;
    std::size_t values = 0;
    const auto keep = [&](std::size_t index) {
        Activation activation = Queue[index];
        if (std::ranges::find(ConsumedThisPass, std::pair{ activation.Recipient, activation.KeyHash })
            != ConsumedThisPass.end())
        {
            ++Counts.Gated;
            return;
        }
        if (activation.Values != values)
            std::move(Values.begin() + activation.Values, Values.begin() + activation.Values + activation.ValueCount,
                      Values.begin() + static_cast<std::ptrdiff_t>(values));
        activation.Values = static_cast<std::uint32_t>(values);
        values += activation.ValueCount;
        Queue[entries++] = activation;
    };
    for (const std::size_t index : Carried)
        keep(index);
    for (std::size_t index = waiting; index < Queue.size(); ++index)
        keep(index);
    Queue.resize(entries);
    Values.resize(values);
}

void ResponsesSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    if (ResponsesComponent == InvalidComponentId && world.IsRegistered<Responses>())
        ResponsesComponent = world.GetComponentId<Responses>();
    const SensorOverlaps* overlaps = world.TryGetResource<SensorOverlaps>();
    if (!IsSimulationAuthority(world))
    {
        // A client runs nothing and remembers nothing it would have to unlearn.
        Queue.clear();
        Values.clear();
        Cooldowns.clear();
        if (SensorInteractions* requests = world.TryGetResource<SensorInteractions>())
            requests->Requests.clear();
        if (overlaps != nullptr && !overlaps->Occurrences().empty())
        {
            LastSampleTick = overlaps->Occurrences().front().Tick;
            SampledBefore = true;
        }
        return;
    }
    if (ResponsesComponent == InvalidComponentId)
        return;

    // Compile stamps are read once here; every resolve this pass compares against them.
    if (ResponseDefinitions* definitions = world.TryGetResource<ResponseDefinitions>())
    {
        definitions->Sequences.Revalidate(world);
        definitions->Sets.Revalidate(world);
    }
    ResolveSensorEvents();
    if (overlaps != nullptr)
        Observe(*overlaps);
    AnswerInteractions(world, overlaps, ctx.Time.TickIndex);
    if (!Queue.empty())
        RunQueue(ctx);
    if (!Cooldowns.empty())
        std::erase_if(Cooldowns, [&](const auto& entry) { return entry.second <= ctx.Time.TickIndex; });
}

void ResponsesSystem::CaptureZoneState(World& world, const ZoneResidencyChange& change) const
{
    ZoneStateStore* zoneState = world.TryGetResource<ZoneStateStore>();
    const ResponseDefinitions* definitions = world.TryGetResource<ResponseDefinitions>();
    if (zoneState == nullptr || definitions == nullptr || !world.IsRegistered<PersistentIdComponent>())
        return;
    StoragePartitionSet detaching;
    detaching.Add(change.Partition);
    std::vector<RememberedBinding> remembered;
    Query<Read<PersistentIdComponent>, Read<Responses>> holders(world);
    holders.ForEachChunkIn(detaching, [&](auto& view) {
        const auto ids = view.template Read<PersistentIdComponent>();
        const auto responses = view.template Read<Responses>();
        for (std::size_t row = 0; row < ids.size(); ++row)
        {
            if (!ids[row].Id.IsValid())
                continue;
            const Responses& state = responses[row];
            const std::span<const ResponseBindingDesc> bindings = definitions->Sets.Bindings(state.Set);
            const std::uint32_t authored = definitions->Sets.StartsDisabled(state.Set);
            remembered.clear();
            for (std::size_t i = 0; i < bindings.size(); ++i)
            {
                const std::uint32_t bit = 1u << i;
                if (((state.Disabled ^ authored) & bit) == 0 && (state.Consumed & bit) == 0)
                    continue;
                remembered.push_back(RememberedBinding{ .KeyHash = bindings[i].KeyHash,
                                                        .DefinitionHash = bindings[i].DefinitionHash,
                                                        .Disabled = (state.Disabled & bit) != 0 ? std::uint8_t{ 1 }
                                                                                                : std::uint8_t{ 0 },
                                                        .Consumed = (state.Consumed & bit) != 0 ? std::uint8_t{ 1 }
                                                                                                : std::uint8_t{ 0 } });
            }
            if (remembered.empty())
                zoneState->ForgetComponentState(change.Zone, ids[row].Id, ResolveComponentTypeId<Responses>());
            else
                zoneState->RecordComponentState(change.Zone, ids[row].Id, ResolveComponentTypeId<Responses>(),
                                                std::as_bytes(std::span(remembered)));
        }
    });
}

void ResponsesSystem::RestoreZoneState(World& world, const ZoneResidencyChange& change) const
{
    const ZoneStateStore* zoneState = world.TryGetResource<ZoneStateStore>();
    const ResponseDefinitions* definitions = world.TryGetResource<ResponseDefinitions>();
    if (zoneState == nullptr || definitions == nullptr || !world.IsRegistered<PersistentIdComponent>())
        return;
    StoragePartitionSet attached;
    attached.Add(change.Partition);
    Query<Read<PersistentIdComponent>, Write<Responses>> holders(world);
    holders.ForEachChunkIn(attached, [&](auto& view) {
        const auto ids = view.template Read<PersistentIdComponent>();
        auto responses = view.template Write<Responses>();
        for (std::size_t row = 0; row < ids.size(); ++row)
        {
            const std::span<const std::byte> bytes =
                ids[row].Id.IsValid() ? zoneState->ComponentState(change.Zone, ids[row].Id,
                                                                  ResolveComponentTypeId<Responses>())
                                      : std::span<const std::byte>();
            if (bytes.empty() || bytes.size() % sizeof(RememberedBinding) != 0)
                continue;
            Responses& state = responses[row];
            const std::span<const ResponseBindingDesc> bindings = definitions->Sets.Bindings(state.Set);
            for (std::size_t offset = 0; offset < bytes.size(); offset += sizeof(RememberedBinding))
            {
                RememberedBinding binding;
                std::memcpy(&binding, bytes.data() + offset, sizeof(binding));
                const int index = FindResponseBinding(bindings, binding.KeyHash);
                // Remembered only for the text it had: a binding authored anew starts anew.
                if (index < 0 || bindings[static_cast<std::size_t>(index)].DefinitionHash != binding.DefinitionHash)
                    continue;
                const std::uint32_t bit = 1u << static_cast<std::uint32_t>(index);
                state.Disabled = binding.Disabled != 0 ? (state.Disabled | bit) : (state.Disabled & ~bit);
                state.Consumed = binding.Consumed != 0 ? (state.Consumed | bit) : (state.Consumed & ~bit);
            }
        }
    });
}

void ResponsesSystem::ZoneResidency(ZoneResidencyContext& ctx)
{
    World& world = ctx.Entities;
    if (ResponsesComponent == InvalidComponentId)
        return;
    for (const ZoneResidencyChange& change : ctx.Changes)
    {
        if (change.Kind == ZoneResidencyChangeKind::Attached)
        {
            RestoreZoneState(world, change);
            continue;
        }
        const bool detaching = change.Kind == ZoneResidencyChangeKind::Detaching;
        if (!detaching && !(change.Previous.Logic && !change.Current.Logic))
            continue;
        if (detaching)
            CaptureZoneState(world, change);
        const auto departing = [&](EntityId entity) {
            return entity.IsValid() && world.IsAlive(entity) && world.GetEntityPartition(entity) == change.Partition;
        };
        // Its waiting activations go with the zone; their payloads are dropped
        // at the next compaction.
        std::erase_if(Queue, [&](const Activation& activation) { return activation.Partition == change.Partition; });
        std::erase_if(Cooldowns, [&](const auto& entry) { return departing(entry.first.Recipient); });
        if (SensorInteractions* requests = world.TryGetResource<SensorInteractions>())
            std::erase_if(requests->Requests,
                          [&](const SensorInteractions::Request& request) { return departing(request.Activator); });
    }
}

void ResponsesSystem::Shutdown()
{
    Queue.clear();
    Values.clear();
    Cooldowns.clear();
}

ResponsesSystem& RegisterResponsesSystem(EngineSchedule& schedule,
                                         World& world,
                                         const FixedSimulationLoop& clock,
                                         VerbDispatcher& verbs,
                                         const AuthoredQueryDispatcher* queries,
                                         const AssetRegistry* assets,
                                         const DataAssetCache* dataAssets,
                                         ConsoleRegistry& console,
                                         LoggingProvider& logging)
{
    if (world.TryGetResource<SensorInteractions>() == nullptr)
        world.AddResource<SensorInteractions>();
    (void)ResponseDefinitionsOf(world);
    ResponsesSystem& system = schedule.Register<ResponsesSystem>(world, clock, verbs, queries, assets, dataAssets,
                                                                 logging.GetLogger<ResponsesSystem>());

    (void)console.RegisterCVar({
        .Name = "logic.responses.queue_capacity",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(system.Capacity()),
        .CurrentValue = static_cast<std::int64_t>(system.Capacity()),
        .Flags = CVarFlags::None,
        .Help = "How many admitted responses may wait to run. Overflow refuses the occurrence "
                "rather than dropping one already admitted.",
        .Source = { "engine" },
        .Min = 1.0,
        .Max = 65536.0,
        .OnChange = [&system](const CVarChangeContext& ctx) {
            system.SetCapacity(static_cast<std::size_t>(std::get<std::int64_t>(ctx.NewValue)));
        },
    });
    (void)console.RegisterCVar({
        .Name = "logic.responses.command_budget",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(system.CommandBudget()),
        .CurrentValue = static_cast<std::int64_t>(system.CommandBudget()),
        .Flags = CVarFlags::None,
        .Help = "How many response commands run in one tick. Responses past it wait for the next tick, in order.",
        .Source = { "engine" },
        .Min = 1.0,
        .Max = 65536.0,
        .OnChange = [&system](const CVarChangeContext& ctx) {
            system.SetCommandBudget(static_cast<std::size_t>(std::get<std::int64_t>(ctx.NewValue)));
        },
    });
    return system;
}
