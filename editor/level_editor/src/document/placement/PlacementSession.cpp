#include "PlacementSession.h"

#include "PlacementCommand.h"

const PendingPlacement* PlacementSession::Pending() const
{
    return Current == PlacementPhase::Idle ? nullptr : &Placement;
}

void PlacementSession::Aim(const PlacementSubject& subject, const PlacementSite& site, float yaw,
                           EditorDocument& document)
{
    if (Current == PlacementPhase::Turning)
        return;
    if (Current == PlacementPhase::Idle || subject != Placement.Subject)
        Placement.Refusal = PlacementRefusal(subject, document);
    Placement.Subject = subject;
    Placement.Site = site;
    Pose(yaw);
    Current = PlacementPhase::Aiming;
}

void PlacementSession::Withdraw()
{
    if (Current == PlacementPhase::Aiming)
        Current = PlacementPhase::Idle;
}

bool PlacementSession::BeginTurn()
{
    if (Current != PlacementPhase::Aiming)
        return false;
    TurnStartYaw = Placement.Yaw;
    Toward.reset();
    Current = PlacementPhase::Turning;
    return true;
}

void PlacementSession::Turn(float yaw, std::optional<Vec3d> toward)
{
    if (Current != PlacementPhase::Turning)
        return;
    Pose(yaw);
    Toward = toward;
}

void PlacementSession::CancelTurn()
{
    if (Current != PlacementPhase::Turning)
        return;
    Pose(TurnStartYaw);
    Toward.reset();
    Current = PlacementPhase::Aiming;
}

std::optional<PendingPlacement> PlacementSession::Commit()
{
    if (Current == PlacementPhase::Idle)
        return std::nullopt;
    Current = PlacementPhase::Idle;
    Toward.reset();
    return Placement;
}

void PlacementSession::Clear()
{
    Current = PlacementPhase::Idle;
    Toward.reset();
}

void PlacementSession::Pose(float yaw)
{
    Placement.Yaw = NormalizedYaw(yaw);
    Placement.Placement = PlacementTransform(Placement.Site, Placement.Yaw);
}
