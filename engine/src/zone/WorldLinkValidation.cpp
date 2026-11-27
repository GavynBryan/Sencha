#include <zone/WorldLinkValidation.h>

#include <math/geometry/3d/ApertureFrame.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <string>
#include <utility>

namespace
{

const AuthoredApertureCookInput* FindAperture(std::span<const AuthoredApertureCookInput> apertures,
                                              PersistentEntityId id)
{
    for (const AuthoredApertureCookInput& aperture : apertures)
        if (aperture.Id == id)
            return &aperture;
    return nullptr;
}

bool Contains(std::span<const ZoneId> zones, ZoneId zone)
{
    return std::find(zones.begin(), zones.end(), zone) != zones.end();
}

// Volumes that share more than a face: zones that only touch are not overlapping.
bool BoundsOverlap(const Aabb3d& a, const Aabb3d& b)
{
    return a.Min.X < b.Max.X && b.Min.X < a.Max.X && a.Min.Y < b.Max.Y && b.Min.Y < a.Max.Y && a.Min.Z < b.Max.Z
        && b.Min.Z < a.Max.Z;
}

bool DockJoins(std::span<const AuthoredDockCookInput> docks, ZoneId a, ZoneId b)
{
    return std::any_of(docks.begin(), docks.end(), [&](const AuthoredDockCookInput& input) {
        return (input.Dock.ZoneA == a && input.Dock.ZoneB == b) || (input.Dock.ZoneA == b && input.Dock.ZoneB == a);
    });
}

const ZoneHeader* FindZone(const WorldPartitionManifest& manifest, ZoneId zone)
{
    for (const ZoneHeader& header : manifest.Zones)
        if (header.Id == zone)
            return &header;
    return nullptr;
}

struct LinkSide
{
    const char* Name;
    const PersistableRef& Ref;
    ZoneId Zone;
};

} // namespace

std::vector<ContentRiskRecord> ValidateAuthoredLinks(const WorldPartitionManifest& manifest,
                                                     std::span<const AuthoredDockCookInput> docks,
                                                     std::span<const AuthoredLinkCookInput> links,
                                                     std::span<const AuthoredApertureCookInput> apertures,
                                                     std::span<const ZoneId> collectedZones)
{
    std::vector<const WorldLink*> sorted;
    sorted.reserve(links.size());
    for (const AuthoredLinkCookInput& input : links)
        sorted.push_back(&input.Link);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const WorldLink* a, const WorldLink* b) { return a->Id.Value < b->Id.Value; });

    std::map<std::uint64_t, std::vector<LinkId>> claims;
    for (const WorldLink* link : sorted)
        for (const PersistableRef* ref : { &link->EndpointA, &link->EndpointB })
        {
            if (!ref->Target.IsValid())
                continue;
            std::vector<LinkId>& claimants = claims[ref->Target.Value];
            if (std::find(claimants.begin(), claimants.end(), link->Id) == claimants.end())
                claimants.push_back(link->Id);
        }

    std::vector<ContentRiskRecord> records;
    const auto report = [&](ContentRiskSeverity severity, LinkId link, std::string rule, std::string message) {
        records.push_back({ .Severity = severity,
                            .Kind = ContentRiskSourceKind::Link,
                            .SourceId = link.Value,
                            .RuleId = std::move(rule),
                            .Message = std::move(message) });
    };

    for (const WorldLink* link : sorted)
    {
        const std::string name = LinkIdToString(link->Id);
        const bool hasA = link->EndpointA.Target.IsValid();
        const bool hasB = link->EndpointB.Target.IsValid();
        if (!hasA && !hasB)
            continue;
        if (hasA != hasB)
        {
            report(ContentRiskSeverity::Error, link->Id, "partition.link.endpoint_one",
                   std::format("link {} names one aperture; a link names two or none", name));
            continue;
        }
        if (link->EndpointA.Target == link->EndpointB.Target)
            report(ContentRiskSeverity::Error, link->Id, "partition.link.endpoint_same",
                   std::format("link {} names aperture {} at both ends", name,
                               PersistentEntityIdToString(link->EndpointA.Target)));

        const LinkSide sides[] = { { "A", link->EndpointA, link->ZoneA }, { "B", link->EndpointB, link->ZoneB } };
        const AuthoredApertureCookInput* found[2] = { nullptr, nullptr };
        bool framesValid = true;
        for (int index = 0; index < 2; ++index)
        {
            const LinkSide& side = sides[index];
            const std::string target = PersistentEntityIdToString(side.Ref.Target);
            const AuthoredApertureCookInput* aperture = FindAperture(apertures, side.Ref.Target);
            found[index] = aperture;
            if (aperture == nullptr)
            {
                framesValid = false;
                if (Contains(collectedZones, side.Zone))
                    report(ContentRiskSeverity::Error, link->Id, "partition.link.endpoint_not_aperture",
                           std::format("link {} end {} names {}, which is no aperture in the loaded zones", name,
                                       side.Name, target));
                continue;
            }
            if (aperture->Zone != side.Zone)
                report(ContentRiskSeverity::Error, link->Id, "partition.link.zone_mismatch",
                       std::format("link {} end {} says zone {}, but aperture {} is in zone {}", name, side.Name,
                                   ZoneDisplayName(manifest, side.Zone), target,
                                   ZoneDisplayName(manifest, aperture->Zone)));
            const ApertureFrameFault fault = CheckApertureFrame(aperture->Transform, aperture->Aperture.HalfExtents);
            if (fault != ApertureFrameFault::None)
            {
                framesValid = false;
                report(ContentRiskSeverity::Error, link->Id, "partition.link.aperture_invalid",
                       std::format("link {} end {}: aperture {} has a {}", name, side.Name, target,
                                   ApertureFrameFaultName(fault)));
            }
            const std::vector<LinkId>& claimants = claims[side.Ref.Target.Value];
            for (const LinkId other : claimants)
                if (other != link->Id)
                    report(ContentRiskSeverity::Error, link->Id, "partition.link.aperture_claimed_twice",
                           std::format("link {} end {}: aperture {} is also named by link {}", name, side.Name,
                                       target, LinkIdToString(other)));
        }

        if (framesValid)
        {
            const Vec2d halfA = found[0]->Aperture.HalfExtents;
            const Vec2d halfB = found[1]->Aperture.HalfExtents;
            if (std::abs(halfA.X - halfB.X) > ApertureTolerance::MatchingExtents
                || std::abs(halfA.Y - halfB.Y) > ApertureTolerance::MatchingExtents)
                report(ContentRiskSeverity::Error, link->Id, "partition.link.extents_differ",
                       std::format("link {} joins apertures of {}x{} and {}x{} half extents", name, halfA.X, halfA.Y,
                                   halfB.X, halfB.Y));
            const ApertureFrame frameA = MakeApertureFrame(found[0]->Transform, halfA);
            const ApertureFrame frameB = MakeApertureFrame(found[1]->Transform, halfB);
            if (!IsUprightMapping(ApertureMapping(frameA, frameB)))
                report(ContentRiskSeverity::Warning, link->Id, "partition.link.not_upright",
                       std::format("link {} does not keep up pointing up, so characters cannot pass it", name));
        }

        const ZoneHeader* zoneA = FindZone(manifest, link->ZoneA);
        const ZoneHeader* zoneB = FindZone(manifest, link->ZoneB);
        if (zoneA != nullptr && zoneB != nullptr && zoneA != zoneB && zoneA->Bounds.IsValid() && zoneB->Bounds.IsValid()
            && BoundsOverlap(zoneA->Bounds, zoneB->Bounds) && !DockJoins(docks, link->ZoneA, link->ZoneB))
            report(ContentRiskSeverity::Error, link->Id, "partition.link.zones_overlap",
                   std::format("link {} joins zones {} and {}, whose bounds overlap with no dock between them", name,
                               ZoneDisplayName(manifest, link->ZoneA), ZoneDisplayName(manifest, link->ZoneB)));
    }

    std::stable_sort(records.begin(), records.end(), [](const ContentRiskRecord& a, const ContentRiskRecord& b) {
        if (a.SourceId != b.SourceId)
            return a.SourceId < b.SourceId;
        return a.RuleId < b.RuleId;
    });

    std::vector<const AuthoredApertureCookInput*> unlinked;
    for (const AuthoredApertureCookInput& aperture : apertures)
        if (!claims.contains(aperture.Id.Value))
            unlinked.push_back(&aperture);
    std::sort(unlinked.begin(), unlinked.end(),
              [](const auto* a, const auto* b) { return a->Id.Value < b->Id.Value; });
    for (const AuthoredApertureCookInput* aperture : unlinked)
        records.push_back({ .Severity = ContentRiskSeverity::Warning,
                            .Kind = ContentRiskSourceKind::Entity,
                            .SourceId = aperture->Id.Value,
                            .RuleId = "partition.link.aperture_unlinked",
                            .Message = std::format("aperture {} in zone {} is named by no link",
                                                   PersistentEntityIdToString(aperture->Id),
                                                   ZoneDisplayName(manifest, aperture->Zone)) });
    return records;
}
