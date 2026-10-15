#include <abilities/AbilityPresentationSystem.h>

#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/CuePresentationLibrary.h>
#include <authored/VerbDispatcher.h>
#include <ecs/World.h>

#include <algorithm>
#include <array>

namespace
{
struct Occurrence
{
    EntityId Actor;
    EntityId Target;
    Vec3d Point = Vec3d::Zero();
    Vec3d Normal = Vec3d::Zero();
    std::int64_t Seed = 0;
    std::int64_t Count = 1;
};

AuthoredValue PortValue(std::string_view port, const Occurrence& occurrence)
{
    const auto vector = [](Vec3d value) {
        return AuthoredValue::Vector({ .Components = { value.X, value.Y, value.Z, 0.0 }, .Length = 3 });
    };
    if (port == "actor")
        return AuthoredValue::Entity(occurrence.Actor);
    if (port == "target")
        return AuthoredValue::Entity(occurrence.Target);
    if (port == "point")
        return vector(occurrence.Point);
    if (port == "normal")
        return vector(occurrence.Normal);
    if (port == "seed")
        return AuthoredValue::Int(occurrence.Seed);
    return AuthoredValue::Int(occurrence.Count);
}

class Presenter
{
public:
    Presenter(World& world, VerbDispatcher& verbs, std::uint64_t tick)
        : Entities(world)
        , Verbs(verbs)
        , Library(world.TryGetResource<CuePresentationLibrary>())
        , Abilities(world.TryGetResource<AbilityRegistry>())
        , Tick(tick)
    {
    }

    [[nodiscard]] bool Ready() const { return Library != nullptr && Abilities != nullptr; }

    void Actor(EntityId actor)
    {
        AbilityPresentationState* state = Entities.TryGet<AbilityPresentationState>(actor);
        const World& reader = Entities;
        const ActiveAbilities* active = reader.TryGet<ActiveAbilities>(actor);
        const AbilityCueLog* log = reader.TryGet<AbilityCueLog>(actor);
        const AbilityPredictedCues* guessed = reader.TryGet<AbilityPredictedCues>(actor);
        if (state == nullptr || active == nullptr || log == nullptr)
            return;
        const bool first = state->Follow == AbilityPresentationFollow::Unseen;
        if (guessed != nullptr)
            Discrete(actor, guessed->Log, *state, state->GuessedCueSeen, first, true);
        Discrete(actor, *log, *state, state->CueSeen, first, false);

        // A record is followed by its identity, not its slot: one the wire or a
        // guess moved to another slot keeps what has been shown of it.
        const auto previous = std::to_array(state->Records);
        std::array<bool, kAbilitySlotCapacity> carried{};
        for (std::size_t slot = 0; slot < kAbilitySlotCapacity; ++slot)
        {
            static const AbilityRecord kNone{};
            const AbilityRecord& record = IsAbilityRecordShadowed(*active, slot) ? kNone : active->Records[slot];
            state->Records[slot] = {};
            for (std::size_t was = 0; was < kAbilitySlotCapacity && record.State != AbilityRecordState::Free; ++was)
                if (!carried[was] && previous[was].Activation == record.Identity && record.Identity != 0)
                {
                    state->Records[slot] = previous[was];
                    carried[was] = true;
                    break;
                }
            if (state->Records[slot].Activation == 0 && record.State != AbilityRecordState::Free)
                for (AbilityPresentationState::Shown& departed : state->Departed)
                    if (departed.Activation == record.Identity && record.Identity != 0)
                    {
                        state->Records[slot] = departed;
                        state->Records[slot].SustainedPhase = kAbilityNotSustained;
                        departed = {};
                        break;
                    }
            Record(actor, record, state->Records[slot], first);
        }
        for (std::size_t was = 0; was < kAbilitySlotCapacity; ++was)
            if (!carried[was] && previous[was].Activation != 0)
            {
                Gone(actor, previous[was]);
                state->Departed[state->NextDeparted++ % kAbilitySlotCapacity] = previous[was];
            }
        state->Follow = AbilityPresentationFollow::Following;
    }

private:
    [[nodiscard]] VerbInvocationSource Source(EntityId actor) const
    {
        return { .Producer = actor, .Tick = Tick, .Scope = AuthoredScope::Cosmetic };
    }

    void Fill(const CompiledVerbBinding& binding, const Occurrence& occurrence)
    {
        Inputs.clear();
        for (const VerbCompiledInput& input : binding.Inputs)
            Inputs.push_back(PortValue(input.Name, occurrence));
    }

    void Once(GameplayTagId cue, const Occurrence& occurrence)
    {
        if (const CuePresentationLibrary::Presentation* presentation = Library->Find(cue))
            for (const CompiledVerbBinding& binding : presentation->Once)
            {
                Fill(binding, occurrence);
                (void)Verbs.Invoke(binding, Inputs, Source(occurrence.Actor));
            }
    }

    // A sustained presentation is held by the actor, keyed by the activation, the
    // present entry and the lease, so letting go names exactly what was taken.
    static LeaseOwner OwnerOf(EntityId actor, std::uint64_t activation, std::size_t present, std::size_t binding)
    {
        return { .Entity = actor,
                 .Key = (activation & ~std::uint64_t{ 0xFFFF }) | (static_cast<std::uint64_t>(present) << 8) | binding };
    }

    void Sustain(EntityId actor, std::uint64_t activation, const AbilityPhase& phase, const Occurrence& occurrence)
    {
        for (std::size_t present = 0; present < phase.Present.size(); ++present)
        {
            Once(phase.Present[present], occurrence);
            if (const CuePresentationLibrary::Presentation* presentation = Library->Find(phase.Present[present]))
                for (std::size_t binding = 0; binding < presentation->Sustain.size(); ++binding)
                {
                    Fill(presentation->Sustain[binding], occurrence);
                    (void)Verbs.AcquireLease(presentation->Sustain[binding], Inputs,
                                             OwnerOf(actor, activation, present, binding), Source(actor));
                }
        }
    }

    void Unsustain(EntityId actor, std::uint64_t activation, const AbilityPhase& phase)
    {
        for (std::size_t present = 0; present < phase.Present.size(); ++present)
            if (const CuePresentationLibrary::Presentation* presentation = Library->Find(phase.Present[present]))
                for (std::size_t binding = 0; binding < presentation->Sustain.size(); ++binding)
                    (void)Verbs.ReleaseLease(presentation->Sustain[binding].Verb,
                                             OwnerOf(actor, activation, present, binding), Source(actor));
    }

    // A log's entries since `seen`; a guessed log's are remembered.
    void Discrete(EntityId actor, const AbilityCueLog& log, AbilityPresentationState& state, std::uint16_t& seen,
                  bool first, bool guessing)
    {
        std::array<const AbilityCueEntry*, kAbilityCueLogCapacity> pending{};
        std::size_t count = 0;
        for (const AbilityCueEntry& entry : log.Entries)
            if (entry.Seq != 0 && (first || AbilityCueAfter(entry.Seq, seen)))
                pending[count++] = &entry;
        const std::uint16_t from = seen;
        std::sort(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(count),
                  [from](const AbilityCueEntry* a, const AbilityCueEntry* b) {
                      return static_cast<std::uint16_t>(a->Seq - from) < static_cast<std::uint16_t>(b->Seq - from);
                  });
        for (std::size_t i = 0; i < count; ++i)
        {
            const AbilityCueEntry& entry = *pending[i];
            seen = entry.Seq;
            // An actor seen for the first time presents only what is still recent.
            if (first && entry.Tick + kAbilityCueWindowTicks < Tick)
                continue;
            const std::uint64_t occurrence = AbilityCueOccurrence(entry);
            if (guessing)
                state.Guessed[state.NextGuessed++ % kAbilityGuessedCueMemory] = occurrence;
            else if (std::ranges::find(state.Guessed, occurrence) != std::end(state.Guessed))
                continue;
            Once(entry.Cue, { .Actor = actor, .Target = entry.Target, .Point = entry.Point, .Normal = entry.Normal,
                              .Seed = entry.Seq, .Count = 1 });
        }
        // First sight takes the log as it stands, so serials far along compare from there.
        if (first)
            seen = static_cast<std::uint16_t>(log.NextSeq - 1);
    }

    void Gone(EntityId actor, const AbilityPresentationState::Shown& shown)
    {
        if (shown.SustainedPhase == kAbilityNotSustained)
            return;
        if (const AbilityDefinition* gone = Abilities->Get(shown.Ability);
            gone != nullptr && shown.SustainedPhase < gone->Phases.size())
            Unsustain(actor, shown.Activation, gone->Phases[shown.SustainedPhase]);
    }

    void Record(EntityId actor, const AbilityRecord& record, AbilityPresentationState::Shown& shown, bool first)
    {
        if (record.State == AbilityRecordState::Free)
            return;
        if (shown.Activation != record.Identity)
        {
            shown = {};
            shown.Activation = record.Identity;
            shown.Ability = record.Key.Ability;
            shown.Pulses = first ? record.Pulse : 0;
        }
        const AbilityDefinition* definition = Abilities->Get(record.Key.Ability);
        const bool known = definition != nullptr && record.Phase < definition->Phases.size();
        const Occurrence occurrence{ .Actor = actor, .Target = record.LastTarget, .Point = record.LastPoint,
                                     .Normal = record.LastNormal, .Seed = record.Pulse, .Count = 1 };
        if (record.Pulse > shown.Pulses && known)
        {
            Occurrence pulsed = occurrence;
            pulsed.Count = std::min(record.Pulse - shown.Pulses, kAbilityPulsesPerOccurrence);
            for (const std::uint16_t op : definition->Phases[record.Phase].PulseOps)
                if (definition->Ops[op].Kind == AbilityOpKind::Cue)
                    Once(definition->Ops[op].Cue, pulsed);
        }
        shown.Pulses = record.Pulse;

        const bool presents = record.State == AbilityRecordState::Live && known
                           && !definition->Phases[record.Phase].Present.empty();
        const std::uint8_t wanted = presents ? record.Phase : kAbilityNotSustained;
        if (shown.SustainedPhase == wanted)
            return;
        if (shown.SustainedPhase != kAbilityNotSustained && definition != nullptr
            && shown.SustainedPhase < definition->Phases.size())
            Unsustain(actor, shown.Activation, definition->Phases[shown.SustainedPhase]);
        if (presents)
            Sustain(actor, shown.Activation, definition->Phases[record.Phase], occurrence);
        shown.SustainedPhase = wanted;
    }

    World& Entities;
    VerbDispatcher& Verbs;
    const CuePresentationLibrary* Library;
    const AbilityRegistry* Abilities;
    std::uint64_t Tick;
    std::vector<AuthoredValue> Inputs;
};
} // namespace

void AbilityPresentationSystem::FixedLogic(FixedLogicContext& ctx)
{
    Present(ctx.Entities, ctx.Time.TickIndex);
}

void AbilityPresentationSystem::Present(World& world, std::uint64_t tick)
{
    if (Verbs == nullptr || !world.IsRegistered<AbilityPresentationState>())
        return;
    Presenter presenter(world, *Verbs, tick);
    if (!presenter.Ready())
        return;
    if (LastWorld != &world)
    {
        Actors.reset();
        LastWorld = &world;
    }
    if (!Actors)
        Actors.emplace(world);
    // Entities first: presenting runs verbs, which may change the World.
    Visited.clear();
    Actors->ForEachChunk([&](auto& view) {
        for (std::uint32_t row = 0; row < view.Count(); ++row)
            Visited.push_back(view.Entity(row));
    });
    for (const EntityId actor : Visited)
        presenter.Actor(actor);
}
