#pragma once

#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

class World;

struct RigidRelocation
{
    // Old space to new space. Must be rigid: unit scale.
    Transform3f Mapping = Transform3f::Identity();
    // Unset: every member stays in the partition it is in. Never inferred from a position.
    std::optional<StoragePartitionId> Partition{};
    // Where in the fixed tick the entity crossed, from 0 to 1, which is where its
    // drawn pose crosses (docs/gameplay/relocation.md, "Presenting").
    float TickFraction = 1.0f;
};

enum class RelocationOutcome : std::uint8_t
{
    Relocated,
    Gone,
    // The root has a parent, so its transform is not in the space the mapping maps.
    Parented,
    NotRigid,
    // A member holds a component whose rule refuses; RelocationResult::Component names it.
    Refused,
};

struct RelocationResult
{
    RelocationOutcome Outcome = RelocationOutcome::Relocated;
    // The refusing component's stable name, when Refused.
    std::string_view Component{};
    // The root's serial after this relocation; zero unless Relocated.
    std::uint32_t Serial = 0;
    // The mapping tilted world up, so state that is held about world up (a yaw) was kept as it was.
    bool KeptUprightState = false;
};

// Moves `root` and its descendants through a rigid mapping: one owner for every
// discontinuous move of an entity. Owner thread, outside any query: it moves
// partitions and may add RelocationSerial. Nothing changes when it refuses.
RelocationResult RelocateEntity(World& world, EntityId root, const RigidRelocation& relocation);

// The stable name of a component in `root`'s group whose rule refuses every
// relocation; empty when there is none.
[[nodiscard]] std::string_view RefusingComponent(const World& world, EntityId root);

// The relocations of the frame so far, in the order they happened. A World
// resource, emptied at the end of the frame (ClearFrameRelocations), so a
// presentation consumer can carry its own history of an entity across.
struct FrameRelocation
{
    EntityId Entity{};
    std::uint32_t Serial = 0;
    Transform3f Mapping = Transform3f::Identity();
    float TickFraction = 1.0f;
};

class FrameRelocations
{
public:
    void Record(const FrameRelocation& relocation) { Entries.push_back(relocation); }
    void Clear() { Entries.clear(); }
    [[nodiscard]] std::span<const FrameRelocation> All() const { return Entries; }

private:
    std::vector<FrameRelocation> Entries;
};

void ClearFrameRelocations(World& world);
