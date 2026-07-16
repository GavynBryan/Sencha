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

bool PositiveAabb(const Aabb3d& bounds)
{
    constexpr float epsilon = 1.0e-4f;
    const bool finite = std::isfinite(bounds.Min.X) && std::isfinite(bounds.Min.Y)
        && std::isfinite(bounds.Min.Z) && std::isfinite(bounds.Max.X)
        && std::isfinite(bounds.Max.Y) && std::isfinite(bounds.Max.Z);
    return finite
        && bounds.Max.X - bounds.Min.X > epsilon
        && bounds.Max.Y - bounds.Min.Y > epsilon
        && bounds.Max.Z - bounds.Min.Z > epsilon;
}

Aabb3d ReflectXZ(const Aabb3d& bounds)
{
    return Aabb3d::FromMinMax(
        Vec3d{ -bounds.Max.X, bounds.Min.Y, -bounds.Max.Z },
        Vec3d{ -bounds.Min.X, bounds.Max.Y, -bounds.Min.Z });
}

std::vector<std::string> ParseCondition(std::string_view condition)
{
    std::vector<std::string> tags;
    std::size_t start = 0;
    while (start < condition.size())
    {
        std::size_t end = condition.find(',', start);
        if (end == std::string_view::npos)
            end = condition.size();
        std::size_t first = start;
        while (first < end && condition[first] == ' ')
            ++first;
        while (end > first && condition[end - 1] == ' ')
            --end;
        if (first < end)
            tags.emplace_back(condition.substr(first, end - first));
        start = end + 1;
    }
    return tags;
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
            || !PositiveAabb(dock.SideAArmBounds) || !PositiveAabb(dock.SideBArmBounds)
            || dock.SideAArmBounds.Max.Z > 1e-4f
            || dock.SideBArmBounds.Min.Z < -1e-4f
            || dock.Directions < 1u || dock.Directions > 3u
            || dock.PreloadDepth < 0)
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

        const std::vector<std::string> requiredTags = ParseCondition(dock.DemandCondition.View());
        DockEndpoint endpointA{
            .Id = dock.Id,
            .OwnerZone = zoneA->Id,
            .OwnerGraph = zoneA->Graph,
            .OtherZone = zoneB->Id,
            .OtherGraph = zoneB->Graph,
            .Side = DockSide::A,
            .Origin = input.Transform.Position,
            .Normal = normal,
            .Right = right,
            .Up = up,
            .HalfExtents = dock.HalfExtents,
            .OwnerArmBoundsLocal = dock.SideAArmBounds,
            .Directions = dock.Directions,
            .PreloadPriority = dock.PreloadPriority,
            .PreloadDepth = dock.PreloadDepth,
            .RequiredTags = requiredTags,
        };
        DockEndpoint endpointB = endpointA;
        endpointB.OwnerZone = zoneB->Id;
        endpointB.OwnerGraph = zoneB->Graph;
        endpointB.OtherZone = zoneA->Id;
        endpointB.OtherGraph = zoneA->Graph;
        endpointB.Side = DockSide::B;
        endpointB.Normal = -normal;
        endpointB.Right = -right;
        endpointB.Up = up;
        endpointB.OwnerArmBoundsLocal = ReflectXZ(dock.SideBArmBounds);

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
            || link.Directions < 1u || link.Directions > 3u
            || link.PreloadDepth < 0)
        {
            SetError(error, std::format("link {} has unsupported semantics",
                                        LinkIdToString(link.Id)));
            return false;
        }

        const std::vector<std::string> requiredTags = ParseCondition(link.DemandCondition.View());
        LinkEndpoint endpointA{
            .Id = link.Id,
            .OwnerZone = zoneA->Id,
            .OwnerGraph = zoneA->Graph,
            .OtherZone = zoneB->Id,
            .OtherGraph = zoneB->Graph,
            .Side = DockSide::A,
            .Kind = static_cast<uint32_t>(link.Kind),
            .Directions = link.Directions,
            .PreloadPriority = link.PreloadPriority,
            .PreloadDepth = link.PreloadDepth,
            .RequiredTags = requiredTags,
        };
        LinkEndpoint endpointB = endpointA;
        endpointB.OwnerZone = zoneB->Id;
        endpointB.OwnerGraph = zoneB->Graph;
        endpointB.OtherZone = zoneA->Id;
        endpointB.OtherGraph = zoneA->Graph;
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
