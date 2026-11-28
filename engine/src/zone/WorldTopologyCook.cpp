#include <zone/WorldTopologyCook.h>

#include <math/geometry/3d/ApertureFrame.h>

#include <algorithm>
#include <format>
#include <optional>
#include <utility>

namespace
{

const ZoneHeader* FindZone(const WorldPartitionManifest& manifest, ZoneId id)
{
    for (const ZoneHeader& zone : manifest.Zones)
        if (zone.Id == id)
            return &zone;
    return nullptr;
}

ZoneHeader* FindZone(WorldPartitionManifest& manifest, ZoneId id)
{
    for (ZoneHeader& zone : manifest.Zones)
        if (zone.Id == id)
            return &zone;
    return nullptr;
}

void SetError(std::string* error, std::string message)
{
    if (error != nullptr)
        *error = std::move(message);
}

std::optional<ApertureFrame> CookedFrame(const AuthoredApertureCookInput* aperture)
{
    if (aperture == nullptr
        || CheckApertureFrame(aperture->Transform, aperture->Aperture.HalfExtents) != ApertureFrameFault::None)
        return std::nullopt;
    return MakeApertureFrame(aperture->Transform, aperture->Aperture.HalfExtents);
}

} // namespace

const AuthoredApertureCookInput* FindAuthoredAperture(std::span<const AuthoredApertureCookInput> apertures,
                                                      PersistentEntityId id)
{
    if (!id.IsValid())
        return nullptr;
    for (const AuthoredApertureCookInput& aperture : apertures)
        if (aperture.Id == id)
            return &aperture;
    return nullptr;
}

bool CookWorldTopology(WorldPartitionManifest& manifest,
                       std::span<const AuthoredDockCookInput> docks,
                       std::span<const AuthoredLinkCookInput> links,
                       std::span<const AuthoredApertureCookInput> apertures,
                       std::string* error)
{
    WorldPartitionManifest cooked = manifest;
    for (ZoneHeader& zone : cooked.Zones)
    {
        zone.Docks.clear();
        zone.Links.clear();
    }
    cooked.DockDebugMap.clear();
    cooked.LinkDebugMap.clear();

    std::vector<AuthoredDockCookInput> sortedDocks(docks.begin(), docks.end());
    std::sort(sortedDocks.begin(), sortedDocks.end(), [](const auto& a, const auto& b)
    {
        if (a.Dock.Id.Value != b.Dock.Id.Value)
            return a.Dock.Id.Value < b.Dock.Id.Value;
        return a.AuthoredEntity < b.AuthoredEntity;
    });
    for (std::size_t index = 0; index < sortedDocks.size(); ++index)
    {
        const AuthoredDockCookInput& input = sortedDocks[index];
        const WorldDock& dock = input.Dock;
        if (!dock.Id.IsValid() || (index > 0 && sortedDocks[index - 1].Dock.Id == dock.Id))
        {
            SetError(error, "world dock ids must be valid and unique");
            return false;
        }
        const ZoneHeader* zoneA = FindZone(std::as_const(cooked), dock.ZoneA);
        const ZoneHeader* zoneB = FindZone(std::as_const(cooked), dock.ZoneB);
        if (zoneA == nullptr || zoneB == nullptr || zoneA == zoneB)
        {
            SetError(error, std::format("dock {} has invalid endpoint zones",
                                        DockIdToString(dock.Id)));
            return false;
        }
        // A dock's rectangle is held to an aperture's rule; only its normal
        // differs, pointing from zone A into zone B.
        const ApertureFrameFault fault = CheckApertureFrame(input.Transform, dock.HalfExtents);
        if (fault == ApertureFrameFault::InvalidExtents || dock.Directions < 1u || dock.Directions > 3u)
        {
            SetError(error, std::format("dock {} has invalid geometry",
                                        DockIdToString(dock.Id)));
            return false;
        }
        if (fault != ApertureFrameFault::None)
        {
            SetError(error, std::format("dock {} has non-finite transform",
                                        DockIdToString(dock.Id)));
            return false;
        }
        ApertureFrame frame = MakeApertureFrame(input.Transform, dock.HalfExtents);
        frame.Normal = -frame.Normal;

        DockEndpoint endpointA{
            .Id = dock.Id,
            .OwnerZone = zoneA->Id,
            .OtherZone = zoneB->Id,
            .Side = DockSide::A,
            .Frame = frame,
            .Directions = dock.Directions,
        };
        DockEndpoint endpointB = endpointA;
        endpointB.OwnerZone = zoneB->Id;
        endpointB.OtherZone = zoneA->Id;
        endpointB.Side = DockSide::B;
        endpointB.Frame.Normal = -frame.Normal;
        endpointB.Frame.Right = -frame.Right;

        FindZone(cooked, zoneA->Id)->Docks.push_back(std::move(endpointA));
        FindZone(cooked, zoneB->Id)->Docks.push_back(std::move(endpointB));
        cooked.DockDebugMap.push_back({ dock.Id, input.AuthoredEntity });
    }

    std::vector<AuthoredLinkCookInput> sortedLinks(links.begin(), links.end());
    std::sort(sortedLinks.begin(), sortedLinks.end(), [](const auto& a, const auto& b)
    {
        if (a.Link.Id.Value != b.Link.Id.Value)
            return a.Link.Id.Value < b.Link.Id.Value;
        return a.AuthoredEntity < b.AuthoredEntity;
    });
    for (std::size_t index = 0; index < sortedLinks.size(); ++index)
    {
        const AuthoredLinkCookInput& input = sortedLinks[index];
        const WorldLink& link = input.Link;
        if (!link.Id.IsValid() || (index > 0 && sortedLinks[index - 1].Link.Id == link.Id))
        {
            SetError(error, "world link ids must be valid and unique");
            return false;
        }
        const bool spatial = link.EndpointA.Target.IsValid() && link.EndpointB.Target.IsValid();
        const ZoneHeader* zoneA = FindZone(std::as_const(cooked), link.ZoneA);
        const ZoneHeader* zoneB = FindZone(std::as_const(cooked), link.ZoneB);
        if (zoneA == nullptr || zoneB == nullptr || (zoneA == zoneB && !spatial))
        {
            SetError(error, std::format("link {} has invalid endpoint zones",
                                        LinkIdToString(link.Id)));
            return false;
        }
        if (link.Kind != LinkKind::Teleport
            || link.Directions < 1u || link.Directions > 3u)
        {
            SetError(error, std::format("link {} has unsupported semantics",
                                        LinkIdToString(link.Id)));
            return false;
        }

        LinkEndpoint endpointA{
            .Id = link.Id,
            .OwnerZone = zoneA->Id,
            .OtherZone = zoneB->Id,
            .Side = DockSide::A,
            .Kind = static_cast<uint32_t>(link.Kind),
            .Directions = link.Directions,
        };
        LinkEndpoint endpointB = endpointA;
        endpointB.OwnerZone = zoneB->Id;
        endpointB.OtherZone = zoneA->Id;
        endpointB.Side = DockSide::B;
        if (spatial)
        {
            const std::optional<ApertureFrame> frameA = CookedFrame(FindAuthoredAperture(apertures, link.EndpointA.Target));
            const std::optional<ApertureFrame> frameB = CookedFrame(FindAuthoredAperture(apertures, link.EndpointB.Target));
            if (frameA.has_value() && frameB.has_value())
            {
                endpointA.Aperture = frameA;
                endpointB.Aperture = frameB;
            }
        }

        FindZone(cooked, zoneA->Id)->Links.push_back(std::move(endpointA));
        FindZone(cooked, zoneB->Id)->Links.push_back(std::move(endpointB));
        cooked.LinkDebugMap.push_back({ link.Id, input.AuthoredEntity });
    }

    for (ZoneHeader& zone : cooked.Zones)
    {
        std::sort(zone.Docks.begin(), zone.Docks.end(), [](const auto& a, const auto& b)
        {
            if (a.Id.Value != b.Id.Value)
                return a.Id.Value < b.Id.Value;
            return a.Side < b.Side;
        });
        std::sort(zone.Links.begin(), zone.Links.end(), [](const auto& a, const auto& b)
        {
            if (a.Id.Value != b.Id.Value)
                return a.Id.Value < b.Id.Value;
            return a.Side < b.Side;
        });
    }
    manifest = std::move(cooked);
    return true;
}
