#include "document/ZoneGraph.h"

#include "document/WorldDocument.h"

#include <zone/WorldConnectionComponents.h>

#include <algorithm>

namespace
{
const ZoneHeader* FindZone(const WorldPartitionManifest& manifest, ZoneId id)
{
    const auto found = std::ranges::find(manifest.Zones, id, &ZoneHeader::Id);
    return found != manifest.Zones.end() && found->Bounds.IsValid() ? &*found : nullptr;
}

struct Connection
{
    ZoneGraphConnection Kind;
    ZoneId A;
    ZoneId B;
    std::uint32_t Directions;
    std::uint64_t StableId;
};
} // namespace

std::vector<ZoneGraphEdge> ZoneGraphEdges(const WorldDocument& world)
{
    if (!world.IsWorld())
        return {};
    const WorldPartitionManifest& manifest = world.Manifest();
    const EditorDocument& scene = world.WorldSceneDocument();
    const World& components = scene.GetRegistry().Components;

    std::vector<std::pair<EntityId, Connection>> connections;
    for (const EntityId entity : scene.GetScene().GetAllEntities())
    {
        if (const WorldDock* dock = components.TryGet<WorldDock>(entity))
            connections.push_back({ entity, { ZoneGraphConnection::Dock, dock->ZoneA, dock->ZoneB, dock->Directions,
                                              dock->Id.Value } });
        if (const WorldLink* link = components.TryGet<WorldLink>(entity))
            connections.push_back({ entity, { ZoneGraphConnection::Link, link->ZoneA, link->ZoneB, link->Directions,
                                              link->Id.Value } });
    }
    std::ranges::sort(connections, {}, [](const auto& entry) { return entry.second.StableId; });

    std::vector<ZoneGraphEdge> edges;
    edges.reserve(connections.size());
    for (const auto& [entity, connection] : connections)
    {
        const ZoneHeader* a = FindZone(manifest, connection.A);
        const ZoneHeader* b = FindZone(manifest, connection.B);
        if (a == nullptr || b == nullptr)
            continue;
        ZoneGraphEdge edge{ .Entity = entity,
                            .Connection = connection.Kind,
                            .A = a->Id,
                            .B = b->Id,
                            .From = a->Bounds.Center(),
                            .To = b->Bounds.Center(),
                            .AToB = (connection.Directions & DockDirectionAToB) != 0,
                            .BToA = (connection.Directions & DockDirectionBToA) != 0,
                            .CrossGraph = a->Graph != b->Graph };
        const ContentRiskSourceKind kind =
            connection.Kind == ZoneGraphConnection::Dock ? ContentRiskSourceKind::Dock : ContentRiskSourceKind::Link;
        for (const ContentRiskRecord& record : world.ValidationRecords())
        {
            if (record.Kind != kind || record.SourceId != connection.StableId)
                continue;
            (record.Severity == ContentRiskSeverity::Error ? edge.Failed : edge.Warned) = true;
        }
        edges.push_back(edge);
    }
    return edges;
}
