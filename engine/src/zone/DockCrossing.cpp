#include <zone/DockCrossing.h>

#include <algorithm>
#include <cmath>

namespace
{

constexpr float SideEpsilon = 1e-4f;

Aabb3d Expanded(Aabb3d bounds, Vec3d amount)
{
    bounds.Min -= amount;
    bounds.Max += amount;
    return bounds;
}

Vec3d ToEndpointLocal(const DockEndpoint& endpoint, Vec3d point)
{
    const Vec3d delta = point - endpoint.Origin;
    return Vec3d{ delta.Dot(endpoint.Right), delta.Dot(endpoint.Up),
                  delta.Dot(endpoint.Normal) };
}

Vec3d ProjectHalfExtent(const DockEndpoint& endpoint, Vec3d worldHalfExtent)
{
    const auto projected = [&](Vec3d axis)
    {
        return std::abs(axis.X) * worldHalfExtent.X
            + std::abs(axis.Y) * worldHalfExtent.Y
            + std::abs(axis.Z) * worldHalfExtent.Z;
    };
    return Vec3d{ projected(endpoint.Right), projected(endpoint.Up),
                  projected(endpoint.Normal) };
}

bool SegmentIntersects(const Aabb3d& bounds, Vec3d from, Vec3d to)
{
    float minimum = 0.0f;
    float maximum = 1.0f;
    const Vec3d delta = to - from;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(delta[axis]) <= SideEpsilon)
        {
            if (from[axis] < bounds.Min[axis] || from[axis] > bounds.Max[axis])
                return false;
            continue;
        }
        float near = (bounds.Min[axis] - from[axis]) / delta[axis];
        float far = (bounds.Max[axis] - from[axis]) / delta[axis];
        if (near > far)
            std::swap(near, far);
        minimum = std::max(minimum, near);
        maximum = std::min(maximum, far);
        if (minimum > maximum)
            return false;
    }
    return true;
}

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

} // namespace

std::optional<ZoneCrossingRecord>
AdvanceZoneFocus(ZoneFocusState& state, const WorldPartitionIndex& index,
                 Vec3d position, DockCrossingOptions options)
{
    if (!state.Current.IsValid())
    {
        state.PreviousPosition = position;
        return std::nullopt;
    }

    bool armedDockStillNear = false;
    for (const DockEndpoint& endpoint : index.DocksFrom(state.Current))
    {
        Aabb3d rearmBounds = endpoint.OwnerArmBoundsLocal;
        // Endpoint arms are owner-local and stop at the plane. Once crossed,
        // keep the same logical Dock armed through the mirrored threshold
        // corridor so tiny back-and-forth motion cannot immediately recross.
        rearmBounds.Max.Z = std::max(rearmBounds.Max.Z, -rearmBounds.Min.Z);
        const Aabb3d arm = Expanded(rearmBounds,
                                    ProjectHalfExtent(endpoint, options.FocusHalfExtent));
        if (endpoint.Id == state.ArmedDock
            && arm.Contains(ToEndpointLocal(endpoint, position)))
            armedDockStillNear = true;
    }
    if (state.ArmedDock.IsValid() && !armedDockStillNear)
        state.ArmedDock = {};

    for (const DockEndpoint& endpoint : index.DocksFrom(state.Current))
    {
        if (!AllowsOutgoing(endpoint) || endpoint.Id == state.ArmedDock)
            continue;
        const Aabb3d arm = Expanded(endpoint.OwnerArmBoundsLocal,
                                    ProjectHalfExtent(endpoint, options.FocusHalfExtent));
        if (!SegmentIntersects(arm, ToEndpointLocal(endpoint, state.PreviousPosition),
                               ToEndpointLocal(endpoint, position)))
            continue;
        const float previousSide = (state.PreviousPosition - endpoint.Origin).Dot(endpoint.Normal);
        const float currentSide = (position - endpoint.Origin).Dot(endpoint.Normal);
        if (previousSide > SideEpsilon || currentSide <= SideEpsilon)
            continue;
        const float denominator = previousSide - currentSide;
        if (std::abs(denominator) <= SideEpsilon)
            continue;
        const float t = previousSide / denominator;
        if (t < 0.0f || t > 1.0f)
            continue;
        const Vec3d intersection = state.PreviousPosition
            + (position - state.PreviousPosition) * t;
        const Vec3d local = intersection - endpoint.Origin;
        if (std::abs(local.Dot(endpoint.Right)) > endpoint.HalfExtents.X
            || std::abs(local.Dot(endpoint.Up)) > endpoint.HalfExtents.Y)
            continue;

        // Keep the sweep start on the source side while streaming catches up.
        // Retrying the same geometric crossing on a later update is then safe
        // even if the focus moved beyond the plane before the zone attached.
        if (!DestinationResident(endpoint.OtherZone, options))
            return std::nullopt;

        ZoneCrossingRecord record{ endpoint.Id, state.Current,
                                   endpoint.OtherZone, intersection };
        state.Previous = state.Current;
        state.Current = endpoint.OtherZone;
        state.ArmedDock = endpoint.Id;
        state.PreviousPosition = position;
        return record;
    }

    state.PreviousPosition = position;
    return std::nullopt;
}
