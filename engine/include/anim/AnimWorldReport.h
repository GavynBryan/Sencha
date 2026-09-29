#pragma once

#include <ecs/EntityId.h>

#include <cstdint>
#include <string>
#include <vector>

class World;

// What one rig's entities in a World carry and have seen.
struct AnimRigWorldReport
{
    std::string RigPath;
    std::uint32_t Entities = 0;
    std::uint32_t MinEntityBytes = 0;
    std::uint32_t MaxEntityBytes = 0;
    // Requests that left their entity's request set without any layer playing them.
    std::uint64_t UnplayedRequests = 0;
    std::uint64_t OrphanedRequests = 0;
};

// Bytes of animation component data `entity` carries.
[[nodiscard]] std::uint32_t AnimEntityBytes(const World& world, EntityId entity);

// One report per rig bound by an entity in `world`, by rig path.
[[nodiscard]] std::vector<AnimRigWorldReport> ReportAnimWorld(World& world);
