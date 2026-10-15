#pragma once

#include <abilities/AbilityLocals.h>
#include <abilities/ActiveAbilities.h>
#include <ecs/EntityId.h>
#include <prediction/PredictionJournal.h>

#include <cstddef>
#include <cstdint>

class VerbDispatcher;
class World;

// The activations a client started for the pawn it predicts, put back beside the
// authority's records after each snapshot until it has decided them
// (docs/gameplay/abilitykit.md, "Prediction").
class AbilityPredictionJournal
{
public:
    static constexpr std::size_t kCapacity = 16;

    // After a client's pass: what each of `actor`'s guesses now is.
    void Note(World& world, EntityId actor);

    // After a snapshot, with the last command the authority ran.
    void Reconcile(World& world, std::uint64_t acknowledgedCommand, VerbDispatcher* verbs);

    [[nodiscard]] std::size_t Size() const { return Guesses.Size(); }

private:
    struct Guess
    {
        EntityId Actor;
        AbilityRecord Record;
        AbilityRecordLocals Locals;
    };

    PredictionJournal<Guess> Guesses{ kCapacity };
};
