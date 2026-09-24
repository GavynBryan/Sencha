#include <navigation/ZoneNavigation.h>

#include "NavTileMeshBackend.h"
#include "ZoneNavigationBackend.h"

#include <gameplay_tags/GameplayTagRegistry.h>
#include <navigation/NavLinkComponent.h>
#include <world/RuntimeWorld.h>

#include <DetourNavMeshQuery.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <tuple>

namespace
{
    // Unique across every load of every zone in the process, so a reference
    // into any earlier load can never match a later one.
    std::uint32_t NextGeneration()
    {
        static std::atomic<std::uint32_t> counter{ 0 };
        return ++counter;
    }

    GameplayTagId Bind(const GameplayTagRegistry* tags, std::string_view name)
    {
        return tags != nullptr && !name.empty() ? tags->FindTag(name) : GameplayTagId{};
    }

    void BindAreas(ZoneNavigation::Backend& zone, std::span<const std::string> names)
    {
        zone.AreaTags.resize(names.size());
        for (std::size_t area = 0; area < names.size(); ++area)
        {
            zone.AreaTags[area] = Bind(zone.Tags, names[area]);
            // The default area needs no tag: it costs 1 unless a policy names it.
            if (!zone.AreaTags[area].IsValid() && area != kNavDefaultArea)
                zone.Diagnostics.push_back(std::format(
                    "navigation area '{}' is not a registered gameplay tag; policies "
                    "cannot price it", names[area]));
        }
    }

    std::uint8_t TraversalKindIndex(ZoneNavigation::Backend& zone, GameplayTagId kind)
    {
        auto found = std::ranges::find(zone.TraversalKinds, kind);
        if (found != zone.TraversalKinds.end())
            return static_cast<std::uint8_t>(found - zone.TraversalKinds.begin());
        if (zone.TraversalKinds.size() >= 64)
            return kUnboundTraversalKind;
        zone.TraversalKinds.push_back(kind);
        return static_cast<std::uint8_t>(zone.TraversalKinds.size() - 1);
    }

    void BindLinks(ZoneNavigation::Backend& zone, std::span<const NavLinkRecord> records)
    {
        zone.Links.reserve(records.size());
        zone.LinkKindIndex.reserve(records.size());
        for (const NavLinkRecord& record : records)
        {
            NavLinkInfo& link = zone.Links.emplace_back();
            link.Id = record.Id;
            link.Traversal = Bind(zone.Tags, record.Traversal);
            link.Directions = record.Directions;
            link.BaseCost = record.BaseCost;
            link.EntryRadius = record.EntryRadius;
            link.Entry = record.Entry;
            link.Exit = record.Exit;
            if (!link.Traversal.IsValid())
                zone.Diagnostics.push_back(std::format(
                    "navigation link {} names traversal '{}', which is not a registered "
                    "gameplay tag; the link is unusable",
                    NavLinkIdToString(record.Id), record.Traversal));
            zone.LinkKindIndex.push_back(link.Traversal.IsValid()
                                             ? TraversalKindIndex(zone, link.Traversal)
                                             : kUnboundTraversalKind);
        }
    }

    std::optional<NavTileRange> TileRectangle(std::span<const NavTileRecord> tiles)
    {
        if (tiles.empty())
            return std::nullopt;
        NavTileRange range{ tiles.front().Coord, tiles.front().Coord };
        for (const NavTileRecord& tile : tiles)
        {
            range.Min = NavTileCoord{ std::min(range.Min.X, tile.Coord.X),
                                      std::min(range.Min.Z, tile.Coord.Z) };
            range.Max = NavTileCoord{ std::max(range.Max.X, tile.Coord.X),
                                      std::max(range.Max.Z, tile.Coord.Z) };
        }
        return range;
    }

    // Loads one profile's cooked tiles. False when the profile is unusable.
    bool LoadProfile(ZoneNavigation::Backend& zone, NavProfileRecord& record)
    {
        ZoneNavProfile& profile = zone.Profiles.emplace_back();
        profile.Name = record.Name;
        profile.Tag = Bind(zone.Tags, record.Name);
        if (!profile.Tag.IsValid())
            zone.Diagnostics.push_back(std::format(
                "navigation profile '{}' is not a registered gameplay tag; queries "
                "cannot name it", record.Name));

        profile.CookedTiles = TileRectangle(record.Tiles);
        const std::uint32_t capacity = profile.CookedTiles
            ? static_cast<std::uint32_t>((profile.CookedTiles->Max.X - profile.CookedTiles->Min.X + 1)
                                         * (profile.CookedTiles->Max.Z - profile.CookedTiles->Min.Z + 1))
            : 1u;
        if (!profile.Mesh.Init(record.Build, capacity))
        {
            zone.Diagnostics.push_back(std::format(
                "navigation profile '{}' has invalid build parameters", record.Name));
            return false;
        }
        for (NavTileRecord& tile : record.Tiles)
        {
            if (!tile.Data.empty() && !profile.Mesh.SetTile(tile.Coord, tile.Data))
                zone.Diagnostics.push_back(std::format(
                    "navigation profile '{}' tile ({}, {}) failed to load", record.Name,
                    tile.Coord.X, tile.Coord.Z));
            tile.Data.clear(); // the mesh holds its own copy
        }
        profile.SourceTiles = std::move(record.Tiles);
        std::ranges::sort(profile.SourceTiles, {}, &NavTileRecord::Coord);
        AttachNavLinks(zone, profile, nullptr);
        return true;
    }

    void RebuildLinkEdges(const ZoneNavigation::Backend& zone, ZoneNavProfile& profile)
    {
        profile.Edges.clear();
        for (std::uint32_t link = 0; link < zone.Links.size(); ++link)
        {
            const NavLinkEndpoints& ends = profile.Endpoints[link];
            if (!ends.Attached() || zone.LinkKindIndex[link] == kUnboundTraversalKind)
                continue;
            if ((zone.Links[link].Directions & NavLinkDirectionForward) != 0)
                profile.Edges.push_back(NavLinkEdge{ ends.Entry.Polygon, link, true });
            if ((zone.Links[link].Directions & NavLinkDirectionReverse) != 0)
                profile.Edges.push_back(NavLinkEdge{ ends.Exit.Polygon, link, false });
        }
        std::ranges::sort(profile.Edges, {}, [](const NavLinkEdge& edge)
                          { return std::tuple(edge.From, edge.Link, !edge.Forward); });
    }

    bool SameAttachment(const NavAnchorAttachment& a, const NavAnchorAttachment& b)
    {
        return a.Polygon == b.Polygon && a.Position == b.Position;
    }
}

void AttachNavLinks(ZoneNavigation::Backend& zone, ZoneNavProfile& profile,
                    std::vector<std::uint32_t>* moved)
{
    std::vector<NavLinkEndpoints> endpoints(zone.Links.size());
    const dtNavMesh* mesh = profile.Mesh.GetBackend().Mesh;
    // Owner-thread maintenance at load and after rebuilds, never on the query
    // path, so a transient query object is acceptable.
    dtNavMeshQuery* query = mesh != nullptr && !zone.Links.empty() ? dtAllocNavMeshQuery()
                                                                   : nullptr;
    if (query != nullptr && dtStatusSucceed(query->init(mesh, 64)))
        for (std::size_t i = 0; i < zone.Links.size(); ++i)
        {
            const NavLinkInfo& link = zone.Links[i];
            const auto entry = AttachLinkAnchor(profile.Mesh, *query, link.Entry, link.EntryRadius);
            const auto exit = AttachLinkAnchor(profile.Mesh, *query, link.Exit, link.EntryRadius);
            if (entry && exit)
                endpoints[i] = NavLinkEndpoints{ *entry, *exit };
        }
    if (query != nullptr)
        dtFreeNavMeshQuery(query);

    if (moved != nullptr)
        for (std::uint32_t i = 0; i < endpoints.size(); ++i)
            if (i >= profile.Endpoints.size()
                || !SameAttachment(profile.Endpoints[i].Entry, endpoints[i].Entry)
                || !SameAttachment(profile.Endpoints[i].Exit, endpoints[i].Exit))
                moved->push_back(i);
    profile.Endpoints = std::move(endpoints);
    RebuildLinkEdges(zone, profile);
}

const NavTileRecord* FindSourceTile(const ZoneNavProfile& profile, NavTileCoord tile)
{
    const auto found = std::ranges::lower_bound(profile.SourceTiles, tile, {},
                                                &NavTileRecord::Coord);
    return found != profile.SourceTiles.end() && found->Coord == tile ? &*found : nullptr;
}

void BumpNavLinkRevisions(ZoneNavigation::Backend& zone, std::span<const std::uint32_t> links)
{
    for (const std::uint32_t link : links)
        ++zone.Links[link].Revision;
}

ZoneNavigation::ZoneNavigation() : Impl(std::make_unique<Backend>()) {}
ZoneNavigation::~ZoneNavigation() = default;
ZoneNavigation::ZoneNavigation(ZoneNavigation&&) noexcept = default;
ZoneNavigation& ZoneNavigation::operator=(ZoneNavigation&&) noexcept = default;

bool ZoneNavigation::Load(ZoneId zone, NavigationFile file, const GameplayTagRegistry* tags)
{
    Backend& backend = *Impl;
    backend = Backend{};
    backend.Zone = zone;
    backend.Tags = tags;
    backend.Generation = NextGeneration();
    backend.Positions = std::move(file.Positions);
    backend.Indices = std::move(file.Indices);
    backend.MinY = file.MinY;
    backend.MaxY = file.MaxY;
    BindAreas(backend, file.Areas);
    BindLinks(backend, file.Links);

    bool anyProfile = false;
    for (NavProfileRecord& record : file.Profiles)
        anyProfile = LoadProfile(backend, record) || anyProfile;
    return anyProfile;
}

ZoneId ZoneNavigation::Zone() const { return Impl->Zone; }
std::uint32_t ZoneNavigation::Generation() const { return Impl->Generation; }
std::size_t ZoneNavigation::ProfileCount() const { return Impl->Profiles.size(); }

std::optional<std::uint16_t> ZoneNavigation::FindProfile(GameplayTagId profile) const
{
    if (!profile.IsValid())
        return std::nullopt;
    for (std::size_t i = 0; i < Impl->Profiles.size(); ++i)
        if (Impl->Profiles[i].Tag == profile && Impl->Profiles[i].Mesh.TileCount() > 0)
            return static_cast<std::uint16_t>(i);
    return std::nullopt;
}

const std::string& ZoneNavigation::ProfileName(std::uint16_t profile) const
{
    return Impl->Profiles[profile].Name;
}

const NavTileMesh& ZoneNavigation::Mesh(std::uint16_t profile) const
{
    return Impl->Profiles[profile].Mesh;
}

std::span<const GameplayTagId> ZoneNavigation::AreaTags() const { return Impl->AreaTags; }
std::size_t ZoneNavigation::LinkCount() const { return Impl->Links.size(); }

std::optional<std::uint32_t> ZoneNavigation::FindLink(NavLinkId id) const
{
    const auto found = std::ranges::lower_bound(Impl->Links, id.Value, {},
                                                [](const NavLinkInfo& link) { return link.Id.Value; });
    if (found == Impl->Links.end() || found->Id != id)
        return std::nullopt;
    return static_cast<std::uint32_t>(found - Impl->Links.begin());
}

const NavLinkInfo& ZoneNavigation::Link(std::uint32_t index) const
{
    return Impl->Links[index];
}

void ZoneNavigation::SetLinkState(std::uint32_t index, bool enabled, float costScale)
{
    NavLinkInfo& link = Impl->Links[index];
    if (!std::isfinite(costScale) || costScale < 0.0f)
        costScale = 1.0f;
    if (link.Enabled == enabled && link.CostScale == costScale)
        return;
    link.Enabled = enabled;
    link.CostScale = costScale;
    ++link.Revision;
}

std::span<const std::string> ZoneNavigation::Diagnostics() const
{
    return Impl->Diagnostics;
}

bool ReadZoneNavigationFile(const std::string& cookedScenePath, NavigationFile& file,
                            std::string* error)
{
    constexpr std::string_view cookedSuffix = ".smap";
    if (!cookedScenePath.ends_with(cookedSuffix))
        return false;
    const std::string path = cookedScenePath.substr(0, cookedScenePath.size() - cookedSuffix.size())
                           + "/navigation.snav";
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
        return false;
    const std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                                  std::istreambuf_iterator<char>());
    std::string decodeError;
    if (!DecodeNavigationFile(std::as_bytes(std::span(chars)), file, &decodeError))
    {
        if (error != nullptr)
            *error = path + ": " + decodeError;
        return false;
    }
    return true;
}

ZoneNavigation* FindZoneNavigation(RuntimeWorld& runtime, ZoneId zone)
{
    RuntimeZoneRecord* record = runtime.FindZone(zone);
    return record != nullptr && record->State == RuntimeZoneLoadState::Resident
        ? record->Resources.TryGet<ZoneNavigation>() : nullptr;
}

const ZoneNavigation* FindZoneNavigation(const RuntimeWorld& runtime, ZoneId zone)
{
    const RuntimeZoneRecord* record = runtime.FindZone(zone);
    return record != nullptr && record->State == RuntimeZoneLoadState::Resident
        ? record->Resources.TryGet<ZoneNavigation>() : nullptr;
}

ZoneNavigation* AttachZoneNavigation(RuntimeWorld& runtime, RuntimeZoneRecord& zone,
                                     NavigationFile file)
{
    ZoneNavigation navigation;
    const GameplayTagRegistry* tags =
        std::as_const(runtime.Entities()).TryGetResource<GameplayTagRegistry>();
    if (!navigation.Load(zone.Id, std::move(file), tags))
        return nullptr;
    return &zone.Resources.Register<ZoneNavigation>(std::move(navigation));
}
