#pragma once

#include "PendingPlacement.h"

#include <math/Vec.h>

#include <cstdint>
#include <optional>

class EditorDocument;

enum class PlacementPhase : std::uint8_t
{
    Idle,    // nothing pending
    Aiming,  // the pending placement follows the pointer
    Turning, // its site is held and its yaw follows the pointer
};

// The one placement pending in a level workspace and its phase. Gestures move
// it; the ghost and marks only read it; a commit hands back what they read.
// A transition that does not apply in the current phase does nothing.
class PlacementSession
{
public:
    [[nodiscard]] PlacementPhase Phase() const { return Current; }
    // Null while Idle.
    [[nodiscard]] const PendingPlacement* Pending() const;
    // Where a turn is facing, while it has a point to face.
    [[nodiscard]] const std::optional<Vec3d>& TurnToward() const { return Toward; }

    // Idle or Aiming: the placement now pending. The document judges the
    // subject when it changes and when aiming starts, never per move.
    void Aim(const PlacementSubject& subject, const PlacementSite& site, float yaw, EditorDocument& document);
    // Aiming: nothing pending.
    void Withdraw();

    // Aiming: holds the site for a turn. False when there was nothing to turn.
    bool BeginTurn();
    // Turning: the yaw now pending, and the point it faces when there is one.
    void Turn(float yaw, std::optional<Vec3d> toward);
    // Turning: back to aiming at the yaw the turn began from.
    void CancelTurn();

    // Aiming or Turning: the pending placement exactly as it was shown; the
    // session goes Idle. MakePlacementCommand judges it.
    [[nodiscard]] std::optional<PendingPlacement> Commit();

    void Clear();

private:
    void Pose(float yaw);

    PlacementPhase Current = PlacementPhase::Idle;
    PendingPlacement Placement;
    float TurnStartYaw = 0.0f;
    std::optional<Vec3d> Toward;
};
