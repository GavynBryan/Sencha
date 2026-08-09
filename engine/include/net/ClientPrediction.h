#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>
#include <movement/JumpState.h>
#include <movement/MovementComponents.h>
#include <net/PawnCommandRing.h>
#include <world/transform/TransformComponents.h>

#include <cstddef>
#include <cstdint>
#include <span>

class World;
class WorldComponentSchema;

//=============================================================================
// ClientPrediction
//
// The one entity a client simulates for itself, and what it keeps in order to
// be put back in step with the authority.
//
// Everything else a client holds is a puppet: it arrives as state and is drawn
// where the authority last said. Its own pawn cannot be, because the player is
// holding the key now and would watch the reply arrive. So the client runs the
// same movement over the same input and moves immediately.
//
// Being wrong is therefore normal, and the design is about how being wrong gets
// resolved. This keeps the two things that resolve it: the authority's own view
// of the pawn -- apart from the world's copy, which is this machine's guess --
// and every tick of input the authority has not answered yet. Given both, a
// client can start again from what the authority did and re-run what it has not
// seen, which leaves nowhere for a disagreement to accumulate. PawnStateReplay
// is the step that does it.
//
// Pure: no World in the state, no session, no clock.
//=============================================================================
class ClientPrediction
{
public:
    // Which entity this machine simulates rather than mirrors. Invalid means
    // none, which is every client before it has been given a pawn and every
    // process that is not a client at all.
    void SetPredicted(EntityId entity);
    [[nodiscard]] EntityId Predicted() const { return Subject; }
    [[nodiscard]] bool Predicts(EntityId entity) const
    {
        return Subject.IsValid() && entity == Subject;
    }

    // Off leaves the pawn where the authority put it without re-running the
    // ticks since, which is the input-delay behaviour prediction replaced: an
    // input costs a round trip before anyone sees it. Kept so the difference
    // can be watched rather than argued about.
    void SetEnabled(bool enabled) { Enabled = enabled; }
    [[nodiscard]] bool IsEnabled() const { return Enabled; }

    // Every tick this machine has simulated for its own pawn, under the name
    // the authority knows it by. The one source for what goes on the wire and
    // for what a replay re-runs.
    [[nodiscard]] PawnCommandRing& Commands() { return Ring; }
    [[nodiscard]] const PawnCommandRing& Commands() const { return Ring; }

    //-------------------------------------------------------------------------
    // The authority's own view
    //
    // Kept apart from the world's copy, which holds what this machine guessed.
    // A snapshot carries only what changed since the authority's baseline, so a
    // delta has to be applied to what the authority believes it sent. Applied
    // to the guess instead, a field it had no reason to resend would be filled
    // in from this machine's own invention -- and the divergence in exactly
    // that field would read as perfect agreement.
    //
    // The set is the pawn's movement state: where it is, how fast, what it is
    // standing on, which rules it moves under, and whether it may jump. A
    // replay resumes from all of it, because resuming from the position alone
    // puts the pawn in the right place still carrying the wrong everything
    // else.
    //-------------------------------------------------------------------------
    [[nodiscard]] bool Intercepts(EntityId entity, ComponentTypeId type) const;
    [[nodiscard]] std::span<std::byte> AuthoritativeBytes(ComponentTypeId type);
    [[nodiscard]] bool HasAuthoritativeState(ComponentTypeId type) const;
    // Records that the authority has now spoken about this component, so the
    // next delta stages against what it said rather than against a seed.
    void MarkSeen(ComponentTypeId type);
    // Copies every component the authority has spoken about onto the entity.
    [[nodiscard]] bool RestoreTo(World& world, const WorldComponentSchema& schema,
                                 EntityId entity) const;

    //-------------------------------------------------------------------------
    // What the stats surface reads
    //
    // Reset distance is the one that matters: how far the pawn moved when the
    // authority's state replaced this machine's guess and the unanswered ticks
    // were re-run. A steady few centimetres is a healthy connection. A rising
    // floor is the two simulations drifting apart faster than they are being
    // pulled together, which is the shape of the failure this exists to make
    // impossible.
    //-------------------------------------------------------------------------
    void NoteReconcile(std::uint32_t ticksReplayed, float resetMeters, bool snapped);
    [[nodiscard]] float LastResetMeters() const { return LastReset; }
    [[nodiscard]] std::uint32_t LastReplayedTicks() const { return LastReplayed; }
    [[nodiscard]] std::uint64_t Reconciles() const { return Reconciled; }
    [[nodiscard]] std::uint64_t Snaps() const { return SnapCount; }

    // Session-transient, and also cleared when the predicted pawn changes: what
    // was kept for one entity says nothing about another.
    void Reset();

private:
    // What the authority last said, one slot per replicated pawn component,
    // written into as bytes because that is what a delta decodes into.
    struct Shadow
    {
        LocalTransform Transform{};
        KinematicState Motion{};
        SupportState Support{};
        CharacterMovement Movement{};
        JumpState Jump{};
    };
    Shadow Authoritative{};

    // Which of them the authority has actually spoken about, so a first delta
    // is staged against something real rather than against zero.
    struct Seen
    {
        bool Transform = false;
        bool Motion = false;
        bool Support = false;
        bool Movement = false;
        bool Jump = false;
    };
    Seen SeenAuthority{};

    PawnCommandRing Ring;

    EntityId Subject;
    bool Enabled = true;

    float LastReset = 0.0f;
    std::uint32_t LastReplayed = 0;
    std::uint64_t Reconciled = 0;
    std::uint64_t SnapCount = 0;
};
