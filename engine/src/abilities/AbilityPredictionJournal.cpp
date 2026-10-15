#include <abilities/AbilityPredictionJournal.h>

#include <abilities/AbilityLeases.h>
#include <ecs/World.h>

#include <algorithm>

namespace
{
// The slot holding `identity`: the authority's record of it, or this machine's guess.
int SlotOf(const ActiveAbilities& active, std::uint64_t identity, bool guessed)
{
    const std::size_t first = guessed ? kAbilityRecordCapacity : 0;
    const std::size_t end = guessed ? kAbilitySlotCapacity : kAbilityRecordCapacity;
    for (std::size_t i = first; i < end; ++i)
    {
        const AbilityRecord& record = active.Records[i];
        if (record.State != AbilityRecordState::Free && record.Identity == identity && record.Predicted == guessed)
            return static_cast<int>(i);
    }
    return -1;
}

int FreeGuessSlot(const ActiveAbilities& active)
{
    for (std::size_t i = kAbilityRecordCapacity; i < kAbilitySlotCapacity; ++i)
        if (active.Records[i].State == AbilityRecordState::Free)
            return static_cast<int>(i);
    return -1;
}

// Back in its own slot, beside the authority's copy rather than over it, so the
// image the next snapshot lands on stays the authority's.
void Place(ActiveAbilities& active, AbilityLocals& locals, const AbilityRecord& guess,
           const AbilityRecordLocals& values)
{
    const int own = SlotOf(active, guess.Identity, true);
    const int slot = own >= 0 ? own : FreeGuessSlot(active);
    if (slot < 0)
        return;
    active.Records[slot] = guess;
    locals.Records[slot] = values;
}

void Forget(ActiveAbilities& active, AbilityLocals& locals, std::uint64_t identity)
{
    if (const int own = SlotOf(active, identity, true); own >= 0)
    {
        active.Records[own] = AbilityRecord{};
        locals.Records[own] = AbilityRecordLocals{};
    }
}
}

bool AbilityPredictionJournal::Track(EntityId actor, const AbilityRecord& record, const AbilityRecordLocals& locals)
{
    return Guesses.Add(Guess{ actor, record, locals }, record.Key.CommandTick);
}

void AbilityPredictionJournal::Note(World& world, EntityId actor)
{
    const ActiveAbilities* active = static_cast<const World&>(world).TryGet<ActiveAbilities>(actor);
    const AbilityLocals* locals = static_cast<const World&>(world).TryGet<AbilityLocals>(actor);
    if (active == nullptr || locals == nullptr)
        return;
    for (std::size_t slot = kAbilityRecordCapacity; slot < kAbilitySlotCapacity; ++slot)
    {
        const AbilityRecord& record = active->Records[slot];
        if (!record.Predicted || record.State == AbilityRecordState::Free)
            continue;
        // A guess the journal has decided is not tracked again while it lingers.
        if (Guess* known = Guesses.Find([&](const Guess& guess) {
                return guess.Actor == actor && guess.Record.Identity == record.Identity;
            }))
        {
            known->Record = record;
            known->Locals = locals->Records[slot];
        }
    }
}

void AbilityPredictionJournal::Reconcile(World& world, std::uint64_t acknowledgedCommand, VerbDispatcher* verbs)
{
    const auto parts = [&](const Guess& guess) -> std::pair<ActiveAbilities*, AbilityLocals*> {
        if (!world.IsAlive(guess.Actor))
            return { nullptr, nullptr };
        return { world.TryGet<ActiveAbilities>(guess.Actor), world.TryGet<AbilityLocals>(guess.Actor) };
    };
    Guesses.Reconcile(
        acknowledgedCommand,
        [&](Guess& guess) {
            const auto [active, locals] = parts(guess);
            if (active == nullptr || locals == nullptr)
                return false;
            const int slot = SlotOf(*active, guess.Record.Identity, false);
            const bool live = guess.Record.State == AbilityRecordState::Live;
            if (slot < 0)
            {
                // The authority ran the command and started nothing: a guess it refused.
                if (live)
                    ReleaseAbilityLeases(verbs, guess.Actor, guess.Record, acknowledgedCommand);
                Forget(*active, *locals, guess.Record.Identity);
                return false;
            }
            const AbilityRecord& word = active->Records[slot];
            // The pulses the authority ran beyond the guess are presented late, never taken back.
            guess.Record.Pulse = std::max(guess.Record.Pulse, word.Pulse);
            if (live && word.State == AbilityRecordState::Lingering)
            {
                // The authority ended it: so does this machine, with the authority's reason.
                ReleaseAbilityLeases(verbs, guess.Actor, guess.Record, acknowledgedCommand);
                guess.Record.State = AbilityRecordState::Lingering;
                guess.Record.EndReason = word.EndReason;
                guess.Record.EndElement = word.EndElement;
                guess.Record.EndTick = word.EndTick;
                Place(*active, *locals, guess.Record, guess.Locals);
                return false;
            }
            Place(*active, *locals, guess.Record, guess.Locals);
            // A guess that ended here is kept while the authority's copy still runs.
            return live || word.State == AbilityRecordState::Live;
        },
        [&](Guess& guess) {
            const auto [active, locals] = parts(guess);
            if (active != nullptr && locals != nullptr)
                Place(*active, *locals, guess.Record, guess.Locals);
        });
}
