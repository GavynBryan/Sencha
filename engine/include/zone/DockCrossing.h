#pragma once

#include <math/Vec.h>
#include <zone/WorldPartitionIndex.h>

#include <optional>
#include <span>

struct ZoneFocusState
{
    ZoneId Current;
    ZoneId Previous;
    DockId ArmedDock;
    Vec3d PreviousPosition;
};

struct ZoneCrossingRecord
{
    DockId Dock;
    ZoneId From;
    ZoneId To;
    Vec3d Position;
};

struct DockCrossingOptions
{
    Vec3d FocusHalfExtent;
    std::span<const ZoneId> ResidentPhysicsZones;
    bool RequireResidentDestination = false;
};

[[nodiscard]] std::optional<ZoneCrossingRecord>
AdvanceZoneFocus(ZoneFocusState& state, const WorldPartitionIndex& index,
                 Vec3d position, DockCrossingOptions options = {});
