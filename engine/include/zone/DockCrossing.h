#pragma once

#include <math/Vec.h>
#include <zone/WorldPartitionIndex.h>

#include <optional>
#include <span>

struct ZoneFocusState
{
    ZoneId Current{};
    ZoneId Previous{};
    DockId SuppressedDock{};
    Vec3d PreviousPosition{};
};

enum class DockTraversalStatus : uint8_t
{
    None,
    Crossed,
    BlockedDestinationNotReady,
};

struct DockTraversalResult
{
    DockTraversalStatus Status = DockTraversalStatus::None;
    DockId Dock{};
    ZoneId From{};
    ZoneId To{};
    Vec3d CrossingPoint{};
    Vec3d SafeSourcePosition{};
    Vec3d PlaneNormal{};
};

struct DockCrossingOptions
{
    float CapsuleRadius = 0.0f;
    float CapsuleHalfHeight = 0.0f;
    std::span<const ZoneId> ResidentPhysicsZones{};
    bool RequireResidentDestination = false;
};

[[nodiscard]] DockTraversalResult
AdvanceZoneFocus(ZoneFocusState& state, const WorldPartitionIndex& index,
                 Vec3d position, DockCrossingOptions options = {});
