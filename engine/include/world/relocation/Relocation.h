#pragma once

#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <math/geometry/3d/Transform3d.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

class World;

struct RigidRelocation
{
    // Old space to new space; unit scale.
    Transform3f Mapping = Transform3f::Identity();
    // Unset: every member stays where it is. Never inferred from a position.
    std::optional<StoragePartitionId> Partition{};
    // Where in the fixed tick it crossed, 0 to 1; zero outside a tick.
    float TickFraction = 0.0f;
};

enum class RelocationOutcome : std::uint8_t
{
    Relocated,
    Gone,
    Parented,
    NotRigid,
    Refused,
};

struct RelocationResult
{
    RelocationOutcome Outcome = RelocationOutcome::Relocated;
    // The refusing component's stable name.
    std::string_view Component{};
    std::uint32_t Serial = 0;
    // The mapping tilts world up, so state held about world up was kept.
    bool KeptUprightState = false;
};

// Owner thread, outside any query. Nothing changes when it refuses.
RelocationResult RelocateEntity(World& world, EntityId root, const RigidRelocation& relocation);

// A World resource: the frame's relocations in order, emptied at the end of the
// frame (docs/gameplay/relocation.md, "FrameRelocations").
struct FrameRelocation
{
    EntityId Entity{};
    std::uint32_t Serial = 0;
    Transform3f Mapping = Transform3f::Identity();
    float TickFraction = 0.0f;
};

class FrameRelocations
{
public:
    void Record(const FrameRelocation& relocation)
    {
        Entries.push_back(relocation);
        MostInAFrame = std::max(MostInAFrame, Entries.size());
    }
    void Clear() { Entries.clear(); }
    [[nodiscard]] std::span<const FrameRelocation> All() const { return Entries; }
    [[nodiscard]] std::size_t HighWater() const { return MostInAFrame; }

private:
    std::vector<FrameRelocation> Entries;
    std::size_t MostInAFrame = 0;
};

void ClearFrameRelocations(World& world);
