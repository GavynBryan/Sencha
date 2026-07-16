#include <zone/WorldTopologyCook.h>

#include <algorithm>
#include <cmath>
#include <format>
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

bool Finite(Vec3d value)
{
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
}

} // namespace

bool CookWorldTopology(WorldPartitionManifest& manifest,
                       std::span<const AuthoredDockCookInput> docks,
                       std::span<const AuthoredLinkCookInput> links,
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
        if (!std::isfinite(dock.HalfExtents.X) || !std::isfinite(dock.HalfExtents.Y)
            || !(dock.HalfExtents.X > 0.0f) || !(dock.HalfExtents.Y > 0.0f)
            || dock.Directions < 1u || dock.Directions > 3u)
        {
            SetError(error, std::format("dock {} has invalid geometry",
                                        DockIdToString(dock.Id)));
            return false;
        }

        const bool unitScale = std::abs(input.Transform.Scale.X - 1.0f) <= 1e-4f
            && std::abs(input.Transform.Scale.Y - 1.0f) <= 1e-4f
            && std::abs(input.Transform.Scale.Z - 1.0f) <= 1e-4f;
        const Vec3d normal = (-input.Transform.Forward()).Normalized();
        const Vec3d right = input.Transform.Right().Normalized();
        const Vec3d up = input.Transform.Up().Normalized();
        if (!unitScale || !Finite(input.Transform.Position) || !Finite(normal)
            || !Finite(right) || !Finite(up)
            || normal.SqrMagnitude() < 0.99f || right.SqrMagnitude() < 0.99f
            || up.SqrMagnitude() < 0.99f)
        {
            SetError(error, std::format("dock {} has non-finite transform",
                                        DockIdToString(dock.Id)));
            return false;
        }

        DockEndpoint endpointA{
            .Id = dock.Id,
            .OwnerZone = zoneA->Id,
            .OtherZone = zoneB->Id,
            .Side = DockSide::A,
            .Origin = input.Transform.Position,
            .Normal = normal,
            .Right = right,
            .Up = up,
            .HalfExtents = dock.HalfExtents,
            .Directions = dock.Directions,
        };
        DockEndpoint endpointB = endpointA;
        endpointB.OwnerZone = zoneB->Id;
        endpointB.OtherZone = zoneA->Id;
        endpointB.Side = DockSide::B;
        endpointB.Normal = -normal;
        endpointB.Right = -right;
        endpointB.Up = up;

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
        const ZoneHeader* zoneA = FindZone(std::as_const(cooked), link.ZoneA);
        const ZoneHeader* zoneB = FindZone(std::as_const(cooked), link.ZoneB);
        if (zoneA == nullptr || zoneB == nullptr || zoneA == zoneB)
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
