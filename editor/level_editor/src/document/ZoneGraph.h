#pragma once

#include <ecs/EntityId.h>
#include <math/Vec.h>
#include <zone/ZoneId.h>

#include <cstdint>
#include <vector>

class WorldDocument;

enum class ZoneGraphConnection : std::uint8_t
{
    Dock,
    Link,
};

// One way between two zones: a dock or a teleport link in the world scene,
// from the centre of one zone's bounds to the other's.
struct ZoneGraphEdge
{
    EntityId Entity;
    ZoneGraphConnection Connection = ZoneGraphConnection::Dock;
    ZoneId A;
    ZoneId B;
    Vec3d From;
    Vec3d To;
    bool AToB = true;
    bool BToA = true;
    bool CrossGraph = false;
    // What the world's validation says about the connection.
    bool Warned = false;
    bool Failed = false;
};

// Every dock and link between two zones that have bounds, in a stable order.
[[nodiscard]] std::vector<ZoneGraphEdge> ZoneGraphEdges(const WorldDocument& world);
