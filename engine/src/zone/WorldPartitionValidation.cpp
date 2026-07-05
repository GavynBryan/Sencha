#include <zone/WorldPartitionValidation.h>

#include <algorithm>
#include <format>
#include <unordered_set>

namespace
{

std::string Hex(uint64_t value)
{
    return std::format("{:016x}", value);
}

// Human-legible reference for a message: the authored name when set, else the
// hex id. Messages are baked at validation time, so a rename that re-runs
// validation re-renders them with the new name.
std::string ZoneLabel(const WorldPartitionManifest& manifest, ZoneId id)
{
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (zone.Id == id)
            return zone.Name.empty() ? Hex(id.Value) : zone.Name;
    }
    return Hex(id.Value);
}

std::string RegionLabel(const WorldPartitionManifest& manifest, RegionId id)
{
    for (const RegionRecord& region : manifest.Regions)
    {
        if (region.Id == id)
            return region.Name.empty() ? Hex(id.Value) : region.Name;
    }
    return Hex(id.Value);
}

// Overlap for validation means interpenetrating volume, not contact. Zones that
// share a face, edge, or corner (the normal way adjacent zones sit against a
// doorway) have zero or negative depth on at least one axis; only positive
// overlap past a small epsilon on all three axes counts. Aabb3d::Intersects is
// deliberately closed (broadphase wants contact), so it is the wrong test here.
bool BoundsInterpenetrate(const Aabb3d& a, const Aabb3d& b)
{
    constexpr double epsilon = 1e-4;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double depth =
            std::min(a.Max[axis], b.Max[axis]) - std::max(a.Min[axis], b.Min[axis]);
        if (depth <= epsilon)
            return false;
    }
    return true;
}

std::string TransitionLabel(const WorldPartitionManifest& manifest,
                            const TransitionRecord& transition)
{
    if (!transition.Name.empty())
        return transition.Name;
    return std::format("{} -> {}", ZoneLabel(manifest, transition.From),
                       ZoneLabel(manifest, transition.To));
}

void AppendSortedBySourceId(std::vector<ContentRiskRecord>& records,
                            std::vector<ContentRiskRecord> ruleRecords)
{
    std::sort(ruleRecords.begin(), ruleRecords.end(),
              [](const ContentRiskRecord& a, const ContentRiskRecord& b)
              { return a.SourceId < b.SourceId; });
    for (ContentRiskRecord& record : ruleRecords)
        records.push_back(std::move(record));
}

void AppendDuplicateIds(std::vector<ContentRiskRecord>& records,
                        std::vector<uint64_t> ids,
                        ContentRiskSourceKind kind,
                        const char* typeName)
{
    std::sort(ids.begin(), ids.end());
    for (size_t i = 1; i < ids.size(); ++i)
    {
        if (ids[i] != ids[i - 1])
            continue;
        records.push_back({
            .Severity = ContentRiskSeverity::Error,
            .Kind = kind,
            .SourceId = ids[i],
            .RuleId = "partition.id.duplicate",
            .Message = std::format("duplicate {} id {}", typeName, Hex(ids[i])),
        });
        // Skip further copies of the same value: one record per duplicated id.
        while (i + 1 < ids.size() && ids[i + 1] == ids[i])
            ++i;
    }
}

} // namespace

std::vector<ContentRiskRecord>
ValidateWorldPartitionManifest(const WorldPartitionManifest& manifest,
                               const WorldPartitionIndex& index)
{
    std::vector<ContentRiskRecord> records;

    // partition.id.duplicate
    {
        std::vector<uint64_t> ids;
        for (const RegionRecord& region : manifest.Regions)
            ids.push_back(region.Id.Value);
        AppendDuplicateIds(records, std::move(ids), ContentRiskSourceKind::Region, "region");

        ids = {};
        for (const ZoneHeader& zone : manifest.Zones)
            ids.push_back(zone.Id.Value);
        AppendDuplicateIds(records, std::move(ids), ContentRiskSourceKind::Zone, "zone");

        ids = {};
        for (const TransitionRecord& transition : manifest.Transitions)
            ids.push_back(transition.Id.Value);
        AppendDuplicateIds(records, std::move(ids), ContentRiskSourceKind::Transition, "transition");
    }

    // partition.zone.region_missing
    {
        std::vector<ContentRiskRecord> rule;
        for (const ZoneHeader& zone : manifest.Zones)
        {
            const bool known = std::any_of(
                manifest.Regions.begin(), manifest.Regions.end(),
                [&](const RegionRecord& region) { return region.Id == zone.Region; });
            if (known)
                continue;
            rule.push_back({
                .Severity = ContentRiskSeverity::Error,
                .Kind = ContentRiskSourceKind::Zone,
                .SourceId = zone.Id.Value,
                .RuleId = "partition.zone.region_missing",
                .Message = std::format("zone {} references missing region {}",
                                       ZoneLabel(manifest, zone.Id),
                                       RegionLabel(manifest, zone.Region)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }

    // partition.transition.endpoint_missing
    {
        std::vector<ContentRiskRecord> rule;
        for (const TransitionRecord& transition : manifest.Transitions)
        {
            if (index.ContainsZone(transition.From) && index.ContainsZone(transition.To))
                continue;
            const ZoneId missing =
                index.ContainsZone(transition.From) ? transition.To : transition.From;
            rule.push_back({
                .Severity = ContentRiskSeverity::Error,
                .Kind = ContentRiskSourceKind::Transition,
                .SourceId = transition.Id.Value,
                .RuleId = "partition.transition.endpoint_missing",
                .Message = std::format("transition {} endpoint {} names no zone",
                                       TransitionLabel(manifest, transition),
                                       Hex(missing.Value)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }

    // partition.transition.self_loop
    {
        std::vector<ContentRiskRecord> rule;
        for (const TransitionRecord& transition : manifest.Transitions)
        {
            if (transition.From != transition.To)
                continue;
            rule.push_back({
                .Severity = ContentRiskSeverity::Error,
                .Kind = ContentRiskSourceKind::Transition,
                .SourceId = transition.Id.Value,
                .RuleId = "partition.transition.self_loop",
                .Message = std::format("transition {} connects zone {} to itself",
                                       TransitionLabel(manifest, transition),
                                       ZoneLabel(manifest, transition.From)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }

    // partition.transition.unpaired
    {
        std::vector<ContentRiskRecord> rule;
        for (const TransitionRecord& transition : manifest.Transitions)
        {
            if (transition.Flags.OneWay || transition.Topology == TransitionTopology::Teleport)
                continue;
            const bool paired = std::any_of(
                manifest.Transitions.begin(), manifest.Transitions.end(),
                [&](const TransitionRecord& other)
                { return other.From == transition.To && other.To == transition.From; });
            if (paired)
                continue;
            rule.push_back({
                .Severity = ContentRiskSeverity::Warning,
                .Kind = ContentRiskSourceKind::Transition,
                .SourceId = transition.Id.Value,
                .RuleId = "partition.transition.unpaired",
                .Message = std::format("transition {} ({} to {}) has no reverse edge",
                                       TransitionLabel(manifest, transition),
                                       ZoneLabel(manifest, transition.From),
                                       ZoneLabel(manifest, transition.To)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }

    // partition.zone.scene_missing
    {
        std::vector<ContentRiskRecord> rule;
        for (const ZoneHeader& zone : manifest.Zones)
        {
            if (!zone.SceneRef.empty())
                continue;
            rule.push_back({
                .Severity = ContentRiskSeverity::Error,
                .Kind = ContentRiskSourceKind::Zone,
                .SourceId = zone.Id.Value,
                .RuleId = "partition.zone.scene_missing",
                .Message = std::format("zone {} has no scene reference",
                                       ZoneLabel(manifest, zone.Id)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }

    // partition.zone.bounds_invalid
    {
        std::vector<ContentRiskRecord> rule;
        for (const ZoneHeader& zone : manifest.Zones)
        {
            if (zone.Bounds.IsValid())
                continue;
            rule.push_back({
                .Severity = ContentRiskSeverity::Error,
                .Kind = ContentRiskSourceKind::Zone,
                .SourceId = zone.Id.Value,
                .RuleId = "partition.zone.bounds_invalid",
                .Message = std::format("zone {} bounds are invalid",
                                       ZoneLabel(manifest, zone.Id)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }

    // partition.bounds.overlap: one record per unordered pair, source id is the
    // lower zone id. Invalid bounds are excluded (bounds_invalid already fired).
    {
        std::vector<const ZoneHeader*> zones;
        for (const ZoneHeader& zone : manifest.Zones)
        {
            if (zone.Bounds.IsValid())
                zones.push_back(&zone);
        }
        std::sort(zones.begin(), zones.end(),
                  [](const ZoneHeader* a, const ZoneHeader* b)
                  { return a->Id.Value < b->Id.Value; });

        std::vector<ContentRiskRecord> rule;
        for (size_t a = 0; a < zones.size(); ++a)
        {
            for (size_t b = a + 1; b < zones.size(); ++b)
            {
                if (zones[a]->Id == zones[b]->Id)
                    continue;
                if (!BoundsInterpenetrate(zones[a]->Bounds, zones[b]->Bounds))
                    continue;
                rule.push_back({
                    .Severity = ContentRiskSeverity::Warning,
                    .Kind = ContentRiskSourceKind::Zone,
                    .SourceId = zones[a]->Id.Value,
                    .RuleId = "partition.bounds.overlap",
                    .Message = std::format("zone {} bounds overlap zone {}",
                                           ZoneLabel(manifest, zones[a]->Id),
                                           ZoneLabel(manifest, zones[b]->Id)),
                });
            }
        }
        records.insert(records.end(), rule.begin(), rule.end());
    }

    // partition.graph.unreachable, suppressed entirely when no_start_zone fires.
    const bool startZoneKnown =
        manifest.StartZone.IsValid() && index.ContainsZone(manifest.StartZone);
    if (startZoneKnown)
    {
        std::unordered_set<uint64_t> reached;
        std::vector<ZoneId> frontier{ manifest.StartZone };
        reached.insert(manifest.StartZone.Value);
        while (!frontier.empty())
        {
            const ZoneId zone = frontier.back();
            frontier.pop_back();
            for (const uint32_t edge : index.Outgoing(zone))
            {
                const ZoneId next = manifest.Transitions[edge].To;
                if (reached.insert(next.Value).second)
                    frontier.push_back(next);
            }
            for (const uint32_t edge : index.Incoming(zone))
            {
                const TransitionRecord& transition = manifest.Transitions[edge];
                if (transition.Flags.OneWay)
                    continue;
                if (reached.insert(transition.From.Value).second)
                    frontier.push_back(transition.From);
            }
        }

        std::vector<ContentRiskRecord> rule;
        for (const ZoneHeader& zone : manifest.Zones)
        {
            if (reached.contains(zone.Id.Value))
                continue;
            rule.push_back({
                .Severity = ContentRiskSeverity::Warning,
                .Kind = ContentRiskSourceKind::Zone,
                .SourceId = zone.Id.Value,
                .RuleId = "partition.graph.unreachable",
                .Message = std::format("zone {} is not reachable from the start zone",
                                       ZoneLabel(manifest, zone.Id)),
            });
        }
        AppendSortedBySourceId(records, std::move(rule));
    }
    else
    {
        records.push_back({
            .Severity = ContentRiskSeverity::Warning,
            .Kind = ContentRiskSourceKind::World,
            .SourceId = 0,
            .RuleId = "partition.world.no_start_zone",
            .Message = "world start zone is not designated or names no zone",
        });
    }

    return records;
}
