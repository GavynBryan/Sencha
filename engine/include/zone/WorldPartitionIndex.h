#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <zone/WorldPartitionManifest.h>

// Derived adjacency over a parsed manifest. Cooked dock and link endpoints are
// indexed by owning zone. Legacy transition indices remain only for editor
// migration diagnostics and are refused by the cooked runtime. Deterministic:
// zones and edge lists are ordered by ascending id value.
class WorldPartitionIndex
{
public:
    static WorldPartitionIndex Build(const WorldPartitionManifest& manifest);

    // Indices into manifest.Transitions, sorted by TransitionId value.
    // Empty span for an unknown zone.
    [[nodiscard]] std::span<const uint32_t> Outgoing(ZoneId zone) const;
    [[nodiscard]] std::span<const uint32_t> Incoming(ZoneId zone) const;
    [[nodiscard]] std::span<const DockEndpoint> DocksFrom(ZoneId zone) const;
    [[nodiscard]] std::span<const LinkEndpoint> LinksFrom(ZoneId zone) const;

    [[nodiscard]] bool ContainsZone(ZoneId zone) const;

private:
    // Sorted zone id array plus offset tables into one shared index buffer;
    // O(zones + endpoints + legacy transitions) memory. Indices_ holds every
    // zone's outgoing run followed by every zone's incoming run; the offset
    // tables hold absolute positions with one trailing end entry each.
    std::vector<uint64_t> ZoneIds_;
    std::vector<uint32_t> OutgoingOffsets_;
    std::vector<uint32_t> IncomingOffsets_;
    std::vector<uint32_t> Indices_;
    std::vector<uint32_t> DockOffsets_;
    std::vector<uint32_t> LinkOffsets_;
    std::vector<DockEndpoint> Docks_;
    std::vector<LinkEndpoint> Links_;

    [[nodiscard]] size_t FindSlot(ZoneId zone) const;
};
