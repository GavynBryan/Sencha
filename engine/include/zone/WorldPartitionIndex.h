#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <zone/WorldPartitionManifest.h>

// Derived adjacency over a parsed manifest: per-zone outgoing and incoming
// transition lists. Built once after parse; the manifest stores each edge exactly
// once and this is the only adjacency representation. Deterministic: zones and
// edge lists are ordered by ascending id value, never by container iteration
// order.
class WorldPartitionIndex
{
public:
    static WorldPartitionIndex Build(const WorldPartitionManifest& manifest);

    // Indices into manifest.Transitions, sorted by TransitionId value.
    // Empty span for an unknown zone.
    [[nodiscard]] std::span<const uint32_t> Outgoing(ZoneId zone) const;
    [[nodiscard]] std::span<const uint32_t> Incoming(ZoneId zone) const;

    [[nodiscard]] bool ContainsZone(ZoneId zone) const;

private:
    // Sorted zone id array plus offset tables into one shared index buffer;
    // O(zones + transitions) memory, binary-search lookup. Indices_ holds every
    // zone's outgoing run followed by every zone's incoming run; the offset
    // tables hold absolute positions with one trailing end entry each.
    std::vector<uint64_t> ZoneIds_;
    std::vector<uint32_t> OutgoingOffsets_;
    std::vector<uint32_t> IncomingOffsets_;
    std::vector<uint32_t> Indices_;

    [[nodiscard]] size_t FindSlot(ZoneId zone) const;
};
