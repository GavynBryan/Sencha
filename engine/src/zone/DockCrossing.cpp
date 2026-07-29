#include <zone/DockCrossing.h>

#include <algorithm>
#include <cmath>

namespace
{

constexpr float SideEpsilon = 1e-4f;
constexpr float HysteresisDistance = 0.05f;

bool AllowsOutgoing(const DockEndpoint& endpoint)
{
    constexpr uint32_t aToB = 1u << 0;
    constexpr uint32_t bToA = 1u << 1;
    return endpoint.Side == DockSide::A
        ? (endpoint.Directions & aToB) != 0
        : (endpoint.Directions & bToA) != 0;
}

bool DestinationResident(ZoneId destination, const DockCrossingOptions& options)
{
    if (!options.RequireResidentDestination)
        return true;
    return std::find(options.ResidentPhysicsZones.begin(),
                     options.ResidentPhysicsZones.end(), destination)
        != options.ResidentPhysicsZones.end();
}

float CapsulePlaneSupport(const DockEndpoint& endpoint,
                          const DockCrossingOptions& options)
{
    return std::max(0.0f, options.CapsuleRadius)
        + std::max(0.0f, options.CapsuleHalfHeight)
            * std::abs(endpoint.Normal.Dot(Vec3d::Up()));
}

} // namespace

DockTraversalResult
AdvanceZoneFocus(ZoneFocusState& state, const WorldPartitionIndex& index,
                  Vec3d position, DockCrossingOptions options)
{
    if (!state.Current.IsValid())
    {
        state.PreviousPosition = position;
        return {};
    }

    bool suppressedDockStillNear = false;
    for (const DockEndpoint& endpoint : index.DocksFrom(state.Current))
    {
        if (endpoint.Id != state.SuppressedDock)
            continue;
        const float side = (position - endpoint.Origin).Dot(endpoint.Normal);
        suppressedDockStillNear = std::abs(side) < HysteresisDistance;
        break;
    }
    if (state.SuppressedDock.IsValid() && !suppressedDockStillNear)
        state.SuppressedDock = {};

    const DockEndpoint* best = nullptr;
    float bestT = 2.0f;
    Vec3d bestIntersection;
    for (const DockEndpoint& endpoint : index.DocksFrom(state.Current))
    {
        if (!AllowsOutgoing(endpoint) || endpoint.Id == state.SuppressedDock)
            continue;
        const float previousSide = (state.PreviousPosition - endpoint.Origin).Dot(endpoint.Normal);
        const float currentSide = (position - endpoint.Origin).Dot(endpoint.Normal);
        const float normalMotion = (position - state.PreviousPosition).Dot(endpoint.Normal);
        const bool sweptAcross = previousSide < -SideEpsilon
            && currentSide > SideEpsilon;
        // A fixed-step sample can land exactly on the plane. Preserve that
        // crossing evidence for the next outward sample instead of replacing
        // it with an unusable zero-side previous position.
        const bool departedPlane = std::abs(previousSide) <= SideEpsilon
            && currentSide > SideEpsilon && normalMotion > SideEpsilon;
        if (!sweptAcross && !departedPlane)
            continue;
        const float denominator = previousSide - currentSide;
        if (std::abs(denominator) <= SideEpsilon)
            continue;
        const float t = departedPlane ? 0.0f : previousSide / denominator;
        if (t < 0.0f || t > 1.0f)
            continue;
        const Vec3d intersection = departedPlane
            ? state.PreviousPosition - endpoint.Normal * previousSide
            : state.PreviousPosition + (position - state.PreviousPosition) * t;
        const Vec3d local = intersection - endpoint.Origin;
        if (std::abs(local.Dot(endpoint.Right)) > endpoint.HalfExtents.X
            || std::abs(local.Dot(endpoint.Up)) > endpoint.HalfExtents.Y)
            continue;
        if (t < bestT || (t == bestT && best != nullptr
                          && endpoint.Id.Value < best->Id.Value))
        {
            best = &endpoint;
            bestT = t;
            bestIntersection = intersection;
        }
    }

    if (best == nullptr)
    {
        state.PreviousPosition = position;
        return {};
    }

    const float support = CapsulePlaneSupport(*best, options);
    const Vec3d safe = bestIntersection
        - best->Normal * (support + HysteresisDistance);
    DockTraversalResult result{
        .Status = DockTraversalStatus::Crossed,
        .Dock = best->Id,
        .From = state.Current,
        .To = best->OtherZone,
        .CrossingPoint = bestIntersection,
        .SafeSourcePosition = safe,
        .PlaneNormal = best->Normal,
    };

    if (!DestinationResident(best->OtherZone, options))
    {
        result.Status = DockTraversalStatus::BlockedDestinationNotReady;
        state.PreviousPosition = safe;
        return result;
    }

    state.Previous = state.Current;
    state.Current = best->OtherZone;
    state.SuppressedDock = best->Id;
    state.PreviousPosition = position;
    return result;
}
