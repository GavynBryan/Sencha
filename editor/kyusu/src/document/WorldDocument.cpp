#include "WorldDocument.h"

#include "PortalGeometry.h"
#include "ZoneBounds.h"
#include "commands/MoveEntitiesToZoneCommand.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <zone/WorldPartitionValidation.h>
#include <zone/ZoneDemand.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <utility>

namespace
{

// Scene file names derive from zone names: lowercase, spaces to underscores,
// ASCII alphanumerics/underscore/hyphen only. Empty after sanitizing falls
// back to "zone".
std::string SanitizeZoneFileName(std::string_view name)
{
    std::string result;
    result.reserve(name.size());
    for (const char c : name)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc > 0x7f)
            continue;
        if (c == ' ')
            result.push_back('_');
        else if (std::isalnum(uc) || c == '_' || c == '-')
            result.push_back(static_cast<char>(std::tolower(uc)));
    }
    if (result.empty())
        result = "zone";
    return result;
}

} // namespace

WorldDocument::WorldDocument(LoggingProvider& logging)
    : Logging_(logging)
    , LegacyDocument_(std::make_unique<EditorDocument>(logging))
    , Rng_(std::random_device{}())
{
}

WorldDocument::~WorldDocument()
{
    WriteUserSidecar();
}

void WorldDocument::SetAssetEnvironment(RuntimeAssets& assets)
{
    Assets_ = &assets;
    if (LegacyDocument_)
        LegacyDocument_->SetAssetEnvironment(assets);
    for (auto& [zone, open] : OpenZones_)
        open.Document->SetAssetEnvironment(assets);
}

bool WorldDocument::LoadWorld(std::string_view path)
{
    auto& log = Logging_.GetLogger<WorldDocument>();

    std::ifstream file{ std::string(path), std::ios::binary };
    if (!file.is_open())
    {
        log.Error("cannot open world file '{}'", path);
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    const std::optional<JsonValue> root = JsonParse(buffer.str(), &parseError);
    if (!root.has_value())
    {
        log.Error("world file '{}' is not valid JSON: {}", path, parseError.Message);
        return false;
    }

    std::string manifestError;
    std::optional<WorldPartitionManifest> manifest =
        ReadWorldPartitionManifest(*root, &manifestError);
    if (!manifest.has_value())
    {
        log.Error("world file '{}' rejected: {}", path, manifestError);
        return false;
    }

    WriteUserSidecar();
    WorldMode_ = true;
    LegacyDocument_.reset();
    OpenZones_.clear();
    FocusZone_ = ZoneId{};
    WorldPath_.assign(path);
    Manifest_ = std::move(*manifest);
    IndexDirty_ = true;
    WorldDirty_ = false;

    ZoneId focus = ApplyUserSidecar();
    if (!focus.IsValid() || FindZoneHeader(focus) == nullptr)
        focus = Manifest_.StartZone;
    if (!focus.IsValid() || FindZoneHeader(focus) == nullptr)
        focus = Manifest_.Zones.empty() ? ZoneId{} : Manifest_.Zones.front().Id;

    if (!focus.IsValid() || !SetFocusZone(focus))
    {
        // A world the editor cannot focus into is unusable: fall back to a fresh
        // legacy document so the workspace always has an editable document.
        log.Error("world file '{}' has no loadable zone to focus", path);
        WorldMode_ = false;
        WorldPath_.clear();
        Manifest_ = {};
        IndexDirty_ = true;
        OpenZones_.clear();
        LegacyDocument_ = std::make_unique<EditorDocument>(Logging_);
        if (Assets_)
            LegacyDocument_->SetAssetEnvironment(*Assets_);
        return false;
    }
    RunValidation();
    return true;
}

bool WorldDocument::SaveWorld()
{
    assert(WorldMode_ && "SaveWorld: world mode only");
    auto& log = Logging_.GetLogger<WorldDocument>();
    if (WorldPath_.empty())
        return false;

    AssignSceneRefsForNewZones();
    // Derived connections land before the zone files write, so the links they
    // stamp onto portals persist in this save.
    ReconcilePortalConnections();

    namespace fs = std::filesystem;
    for (ZoneHeader& header : Manifest_.Zones)
    {
        const std::string scenePath = ResolveScenePath(header.SceneRef);
        std::error_code ec;
        fs::create_directories(fs::path(scenePath).parent_path(), ec);

        const auto it = OpenZones_.find(header.Id);
        if (it != OpenZones_.end())
        {
            EditorDocument& document = *it->second.Document;
            // Derived bounds ride every zone save; designer-set bounds stay
            // authored and are never recomputed. A zone with nothing boundable
            // keeps its previous bounds.
            if (!header.BoundsOverridden)
            {
                if (const auto bounds = ComputeZoneBounds(document.GetScene()))
                    header.Bounds = *bounds;
            }
            if (!document.HasFilePath())
            {
                if (!document.SaveAs(scenePath))
                {
                    log.Error("failed to save zone scene '{}'", scenePath);
                    return false;
                }
            }
            else if (document.IsDirty() && !document.Save())
            {
                log.Error("failed to save zone scene '{}'", scenePath);
                return false;
            }
        }
        else if (!fs::exists(scenePath, ec))
        {
            // Header-only zone that has never had a scene: write an empty one so
            // the manifest never references a missing file.
            EditorDocument empty(Logging_);
            if (!empty.SaveAs(scenePath))
            {
                log.Error("failed to write scene '{}' for zone without content", scenePath);
                return false;
            }
        }
    }

    const std::string text = JsonStringify(WriteWorldPartitionManifest(Manifest_), /*pretty*/ true);
    std::ofstream file(WorldPath_, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        log.Error("cannot write world file '{}'", WorldPath_);
        return false;
    }
    file << text;
    if (!file.good())
        return false;

    WorldDirty_ = false;
    WriteUserSidecar();
    RunValidation();
    return true;
}

bool WorldDocument::SaveWorldAs(std::string_view path)
{
    if (path.empty())
        return false;
    // Whatever the save dialog produced, the world lands with the extension
    // the open routing fast-paths on.
    std::filesystem::path normalized{ std::string(path) };
    if (normalized.extension() != ".sworld")
        normalized.replace_extension(".sworld");
    WorldPath_ = normalized.string();
    return SaveWorld();
}

void WorldDocument::NewWorld(std::string_view name)
{
    WriteUserSidecar();
    WorldMode_ = true;
    LegacyDocument_.reset();
    OpenZones_.clear();
    FocusZone_ = ZoneId{};
    WorldPath_.clear();
    Manifest_ = {};
    Manifest_.Name.assign(name);
    IndexDirty_ = true;

    const RegionId region = AddRegion("Region 1");
    const ZoneId zone = AddZone(region, "Zone 1");
    Manifest_.StartZone = zone;
    WorldDirty_ = true;
    SetFocusZone(zone);
}

bool WorldDocument::HasSaveTarget() const
{
    if (WorldMode_)
        return !WorldPath_.empty();
    return LegacyDocument_->HasFilePath();
}

void WorldDocument::New()
{
    if (WorldMode_)
        CloseWorldToLegacy();
    LegacyDocument_->New();
}

bool WorldDocument::IsWorldManifestFile(std::string_view path)
{
    std::ifstream file(std::string(path), std::ios::binary);
    if (!file.is_open())
        return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const auto json = JsonParse(buffer.str());
    if (!json.has_value() || !json->IsObject())
        return false;
    return json->Find("format_version") != nullptr && json->Find("zones") != nullptr;
}

bool WorldDocument::Load(std::string_view path)
{
    // A world manifest saved under any extension still opens as a world;
    // routing by content keeps a mistyped Save As name from producing a file
    // that only ever opens blank.
    if (IsWorldManifestFile(path))
        return LoadWorld(path);
    if (WorldMode_)
        CloseWorldToLegacy();
    return LegacyDocument_->Load(path);
}

bool WorldDocument::Save()
{
    if (WorldMode_)
        return SaveWorld();
    return LegacyDocument_->Save();
}

bool WorldDocument::SaveAs(std::string_view path)
{
    if (WorldMode_)
        return SaveWorldAs(path);
    return LegacyDocument_->SaveAs(path);
}

WorldPartitionManifest& WorldDocument::Manifest()
{
    assert(WorldMode_ && "Manifest: world mode only");
    return Manifest_;
}

const WorldPartitionIndex& WorldDocument::Index() const
{
    assert(WorldMode_ && "Index: world mode only");
    if (IndexDirty_)
    {
        Index_ = WorldPartitionIndex::Build(Manifest_);
        IndexDirty_ = false;
    }
    return Index_;
}

bool WorldDocument::LoadZone(ZoneId zone)
{
    auto& log = Logging_.GetLogger<WorldDocument>();
    if (!WorldMode_)
        return false;
    if (IsZoneOpen(zone))
        return true;

    const ZoneHeader* header = FindZoneHeader(zone);
    if (header == nullptr)
    {
        log.Error("LoadZone: zone {} is not in the manifest", ZoneIdToString(zone));
        return false;
    }

    auto document = std::make_unique<EditorDocument>(Logging_);
    document->SetRegistryIdentity(RegistryId{ NextRegistryIndex_++, 1 }, zone);
    if (Assets_)
        document->SetAssetEnvironment(*Assets_);

    if (!header->SceneRef.empty())
    {
        const std::string scenePath = ResolveScenePath(header->SceneRef);
        if (!document->Load(scenePath))
        {
            log.Error("LoadZone: cannot load scene '{}' for zone {}", scenePath,
                      ZoneIdToString(zone));
            return false;
        }
    }

    OpenZones_.emplace(zone, OpenZone{ std::move(document), ZoneViewState{} });
    return true;
}

bool WorldDocument::UnloadZone(ZoneId zone)
{
    auto& log = Logging_.GetLogger<WorldDocument>();
    const auto it = OpenZones_.find(zone);
    if (it == OpenZones_.end())
        return false;
    if (zone == FocusZone_)
    {
        log.Warn("UnloadZone: zone {} is the focus zone; focus another zone first",
                 ZoneIdToString(zone));
        return false;
    }
    if (it->second.Document->IsDirty())
    {
        log.Warn("UnloadZone: zone {} has unsaved changes; save first", ZoneIdToString(zone));
        return false;
    }
    OpenZones_.erase(it);
    if (OnZoneUnloaded)
        OnZoneUnloaded(zone);
    return true;
}

bool WorldDocument::IsZoneOpen(ZoneId zone) const
{
    return OpenZones_.contains(zone);
}

EditorDocument* WorldDocument::ZoneDocument(ZoneId zone)
{
    if (!WorldMode_)
        return nullptr;
    const auto it = OpenZones_.find(zone);
    return it != OpenZones_.end() ? it->second.Document.get() : nullptr;
}

bool WorldDocument::SetZoneVisible(ZoneId zone, bool visible)
{
    const auto it = OpenZones_.find(zone);
    if (it == OpenZones_.end())
        return false;
    it->second.View.VisibleInEditor = visible;
    return true;
}

bool WorldDocument::SetFocusZone(ZoneId zone)
{
    if (!WorldMode_)
        return false;
    if (zone == FocusZone_ && IsZoneOpen(zone))
        return true;
    if (!LoadZone(zone))
        return false;

    FocusZone_ = zone;
    if (OnFocusChanged)
        OnFocusChanged();
    return true;
}

EditorDocument& WorldDocument::FocusDocument()
{
    if (!WorldMode_)
        return *LegacyDocument_;
    const auto it = OpenZones_.find(FocusZone_);
    assert(it != OpenZones_.end() && "FocusDocument: focus zone is not open");
    return *it->second.Document;
}

const EditorDocument& WorldDocument::FocusDocument() const
{
    if (!WorldMode_)
        return *LegacyDocument_;
    const auto it = OpenZones_.find(FocusZone_);
    assert(it != OpenZones_.end() && "FocusDocument: focus zone is not open");
    return *it->second.Document;
}

bool WorldDocument::IsDirty() const
{
    if (!WorldMode_)
        return LegacyDocument_->IsDirty();
    if (WorldDirty_)
        return true;
    for (const auto& [zone, open] : OpenZones_)
    {
        if (open.Document->IsDirty())
            return true;
    }
    return false;
}

ZoneId WorldDocument::MintZoneId()
{
    return ZoneId{ MintRawId() };
}

RegionId WorldDocument::MintRegionId()
{
    return RegionId{ MintRawId() };
}

TransitionId WorldDocument::MintTransitionId()
{
    return TransitionId{ MintRawId() };
}

ZoneId WorldDocument::AddZone(RegionId region, std::string name)
{
    assert(WorldMode_ && "AddZone: world mode only");
    ZoneHeader header;
    header.Id = MintZoneId();
    header.Name = std::move(name);
    header.Region = region;
    Manifest_.Zones.push_back(std::move(header));
    MarkManifestEdited();
    RunValidation();
    return Manifest_.Zones.back().Id;
}

RegionId WorldDocument::AddRegion(std::string name)
{
    assert(WorldMode_ && "AddRegion: world mode only");
    RegionRecord record;
    record.Id = MintRegionId();
    record.Name = std::move(name);
    Manifest_.Regions.push_back(std::move(record));
    MarkManifestEdited();
    RunValidation();
    return Manifest_.Regions.back().Id;
}

TransitionId WorldDocument::AddTransition(ZoneId from, ZoneId to, TransitionTopology topology,
                                          bool oneWay, int32_t preloadPriority)
{
    assert(WorldMode_ && "AddTransition: world mode only");
    TransitionRecord record;
    record.Id = MintTransitionId();
    record.From = from;
    record.To = to;
    record.Topology = topology;
    record.Flags.OneWay = oneWay;
    record.PreloadPriority = preloadPriority;
    Manifest_.Transitions.push_back(record);
    MarkManifestEdited();
    RunValidation();
    return record.Id;
}

bool WorldDocument::RemoveTransition(TransitionId transition)
{
    const auto count = std::erase_if(Manifest_.Transitions,
                                     [&](const TransitionRecord& record)
                                     { return record.Id == transition; });
    if (count == 0)
        return false;
    MarkManifestEdited();
    RunValidation();
    return true;
}

void WorldDocument::Revalidate()
{
    ReconcilePortalConnections();
    RunValidation();
}

void WorldDocument::ReconcilePortalConnections()
{
    if (!WorldMode_)
        return;

    // Spans need current bounds; derived ones refresh from live content first
    // (the same recompute a save performs).
    for (ZoneHeader& header : Manifest_.Zones)
    {
        if (header.BoundsOverridden)
            continue;
        const auto it = OpenZones_.find(header.Id);
        if (it == OpenZones_.end())
            continue;
        if (const auto bounds = ComputeZoneBounds(it->second.Document->GetScene()))
            header.Bounds = *bounds;
    }

    // Filing is derived too: a marker belongs to the zone whose bounds
    // contain its center (ResolveFocusZone semantics: the current holder wins
    // while it still contains the center, smallest volume otherwise, sticky
    // when nothing contains it). Wherever it was created, it refiles itself
    // between open zones, so hierarchy placement is never the author's
    // decision. Collected first, applied after: the move mutates entity lists.
    struct PendingRefile
    {
        EntityId Entity;
        ZoneId From;
        ZoneId To;
    };
    std::vector<PendingRefile> refiles;
    for (const ZoneHeader& zone : Manifest_.Zones)
    {
        const auto it = OpenZones_.find(zone.Id);
        if (it == OpenZones_.end())
            continue;
        EditorScene& scene = it->second.Document->GetScene();
        for (EntityId entity : scene.GetAllEntities())
        {
            if (!scene.IsPortal(entity))
                continue;
            const auto bounds = scene.TryGetWorldBounds(entity);
            if (!bounds.has_value())
                continue;
            const ZoneId home = ResolveFocusZone(Manifest_, bounds->Center(), zone.Id);
            if (home.IsValid() && home != zone.Id && OpenZones_.contains(home))
                refiles.push_back({ entity, zone.Id, home });
        }
    }
    for (const PendingRefile& refile : refiles)
    {
        EditorDocument* source = ZoneDocument(refile.From);
        EditorDocument* target = ZoneDocument(refile.To);
        if (source == nullptr || target == nullptr)
            continue;
        const EntityId entities[] = { refile.Entity };
        MoveEntitiesToZoneCommand move(entities, *source, *target);
        move.Execute();
    }

    const auto findRecord = [&](TransitionId id) -> const TransitionRecord*
    {
        for (const TransitionRecord& record : Manifest_.Transitions)
            if (record.Id == id)
                return &record;
        return nullptr;
    };
    const auto findDoorway = [&](ZoneId from, ZoneId to) -> TransitionId
    {
        for (const TransitionRecord& record : Manifest_.Transitions)
            if (record.From == from && record.To == to
                && record.Topology == TransitionTopology::Doorway)
                return record.Id;
        return TransitionId{};
    };

    for (const ZoneHeader& zone : Manifest_.Zones)
    {
        const auto it = OpenZones_.find(zone.Id);
        if (it == OpenZones_.end())
            continue;
        EditorScene& scene = it->second.Document->GetScene();
        for (EntityId entity : scene.GetAllEntities())
        {
            const PortalComponent* portal = scene.TryGetPortal(entity);
            if (portal == nullptr)
                continue;
            const auto bounds = scene.TryGetWorldBounds(entity);
            if (!bounds.has_value())
                continue;   // no geometry to derive from: validation's report

            const ZoneId target = DerivePortalCounterpart(Manifest_, zone.Id, *bounds);
            if (!target.IsValid())
                continue;   // nothing on the other side yet: stays unlinked

            // Geometry wins: the correct link is the Doorway edge leaving this
            // portal's own zone toward the zone its placement spans.
            const TransitionRecord* linked = findRecord(portal->Transition);
            if (linked != nullptr && linked->From == zone.Id && linked->To == target
                && linked->Topology == TransitionTopology::Doorway)
                continue;

            TransitionId forward = findDoorway(zone.Id, target);
            if (!forward.IsValid())
            {
                forward = AddTransition(zone.Id, target, TransitionTopology::Doorway,
                                        /*oneWay*/ false, 0);
                if (!findDoorway(target, zone.Id).IsValid())
                    (void)AddTransition(target, zone.Id, TransitionTopology::Doorway,
                                        /*oneWay*/ false, 0);
            }
            scene.SetComponent(entity, PortalComponent{ forward });
            it->second.Document->MarkDirty();
        }
    }
}

bool WorldDocument::RenameTransition(TransitionId transition, std::string name)
{
    for (TransitionRecord& record : Manifest_.Transitions)
    {
        if (record.Id != transition)
            continue;
        record.Name = std::move(name);
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::SetTransitionTopology(TransitionId transition, TransitionTopology topology)
{
    for (TransitionRecord& record : Manifest_.Transitions)
    {
        if (record.Id != transition)
            continue;
        record.Topology = topology;
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::SetTransitionOneWay(TransitionId transition, bool oneWay)
{
    for (TransitionRecord& record : Manifest_.Transitions)
    {
        if (record.Id != transition)
            continue;
        record.Flags.OneWay = oneWay;
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::SetTransitionRequiredTags(TransitionId transition,
                                              std::vector<std::string> tags)
{
    for (TransitionRecord& record : Manifest_.Transitions)
    {
        if (record.Id != transition)
            continue;
        record.RequiredTags = std::move(tags);
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::SetTransitionPreloadDepth(TransitionId transition, int32_t depth)
{
    for (TransitionRecord& record : Manifest_.Transitions)
    {
        if (record.Id != transition)
            continue;
        record.PreloadDepth = depth < 0 ? 0 : depth;
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::SetTransitionPreloadPriority(TransitionId transition, int32_t priority)
{
    for (TransitionRecord& record : Manifest_.Transitions)
    {
        if (record.Id != transition)
            continue;
        record.PreloadPriority = priority;
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::RenameZone(ZoneId zone, std::string name)
{
    for (ZoneHeader& header : Manifest_.Zones)
    {
        if (header.Id != zone)
            continue;
        header.Name = std::move(name);
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

bool WorldDocument::RenameRegion(RegionId region, std::string name)
{
    for (RegionRecord& record : Manifest_.Regions)
    {
        if (record.Id != region)
            continue;
        record.Name = std::move(name);
        MarkManifestEdited();
        RunValidation();
        return true;
    }
    return false;
}

const ZoneHeader* WorldDocument::FindZoneHeader(ZoneId zone) const
{
    for (const ZoneHeader& header : Manifest_.Zones)
    {
        if (header.Id == zone)
            return &header;
    }
    return nullptr;
}

std::string WorldDocument::ResolveScenePath(std::string_view sceneRef) const
{
    namespace fs = std::filesystem;
    const fs::path ref{ std::string(sceneRef) };
    if (ref.is_absolute() || WorldPath_.empty())
        return ref.string();
    return (fs::path(WorldPath_).parent_path() / ref).string();
}

uint64_t WorldDocument::MintRawId()
{
    std::uniform_int_distribution<uint64_t> distribution;
    for (;;)
    {
        const uint64_t value = distribution(Rng_);
        if (value == 0)
            continue;
        const auto zoneHit = std::any_of(Manifest_.Zones.begin(), Manifest_.Zones.end(),
                                         [&](const ZoneHeader& z) { return z.Id.Value == value; });
        const auto regionHit = std::any_of(Manifest_.Regions.begin(), Manifest_.Regions.end(),
                                           [&](const RegionRecord& r) { return r.Id.Value == value; });
        const auto transitionHit =
            std::any_of(Manifest_.Transitions.begin(), Manifest_.Transitions.end(),
                        [&](const TransitionRecord& t) { return t.Id.Value == value; });
        if (zoneHit || regionHit || transitionHit)
            continue;
        return value;
    }
}

void WorldDocument::MarkManifestEdited()
{
    WorldDirty_ = true;
    IndexDirty_ = true;
}

void WorldDocument::AssignSceneRefsForNewZones()
{
    for (ZoneHeader& header : Manifest_.Zones)
    {
        if (!header.SceneRef.empty())
            continue;

        const std::string base = SanitizeZoneFileName(header.Name);
        std::string candidate = "levels/" + base + ".level.json";
        for (int suffix = 2;; ++suffix)
        {
            const auto taken = std::any_of(
                Manifest_.Zones.begin(), Manifest_.Zones.end(),
                [&](const ZoneHeader& other)
                { return &other != &header && other.SceneRef == candidate; });
            if (!taken)
                break;
            candidate = "levels/" + base + "_" + std::to_string(suffix) + ".level.json";
        }
        header.SceneRef = std::move(candidate);
        MarkManifestEdited();
    }
}

void WorldDocument::RunValidation()
{
    ValidationRecords_.clear();
    if (!WorldMode_)
        return;

    ValidationRecords_ = ValidateWorldPartitionManifest(Manifest_, Index());

    // The pure layer takes no filesystem; resolvability of nonempty scene refs
    // is the editor's half, reported through the same record type.
    std::vector<ContentRiskRecord> unresolved;
    for (const ZoneHeader& zone : Manifest_.Zones)
    {
        if (zone.SceneRef.empty())
            continue;
        std::error_code ec;
        if (std::filesystem::exists(ResolveScenePath(zone.SceneRef), ec))
            continue;
        unresolved.push_back({
            .Severity = ContentRiskSeverity::Error,
            .Kind = ContentRiskSourceKind::Zone,
            .SourceId = zone.Id.Value,
            .RuleId = "partition.zone.scene_unresolved",
            .Message = std::format("zone {} scene '{}' resolves to no file",
                                   ZoneIdToString(zone.Id), zone.SceneRef),
        });
    }
    std::sort(unresolved.begin(), unresolved.end(),
              [](const ContentRiskRecord& a, const ContentRiskRecord& b)
              { return a.SourceId < b.SourceId; });
    for (ContentRiskRecord& record : unresolved)
        ValidationRecords_.push_back(std::move(record));

    // Containment of open-zone content in the zone header's bounds, one record
    // per zone. Zones with derived bounds self-heal on save (the union
    // recompute), so a persistent hit means designer-set bounds, exactly where
    // a straying entity stays wrong until a human acts. Entities without world
    // bounds (nothing boundable) are skipped.
    for (const ZoneHeader& zone : Manifest_.Zones)
    {
        const auto it = OpenZones_.find(zone.Id);
        if (it == OpenZones_.end())
            continue;
        const EditorScene& scene = it->second.Document->GetScene();
        size_t outside = 0;
        for (EntityId entity : scene.GetAllEntities())
        {
            const auto bounds = scene.TryGetWorldBounds(entity);
            if (!bounds.has_value())
                continue;
            if (!zone.Bounds.Contains(bounds->Min) || !zone.Bounds.Contains(bounds->Max))
                ++outside;
        }
        if (outside == 0)
            continue;
        ValidationRecords_.push_back({
            .Severity = ContentRiskSeverity::Warning,
            .Kind = ContentRiskSourceKind::Zone,
            .SourceId = zone.Id.Value,
            .RuleId = "partition.zone.entity_outside_bounds",
            .Message = std::format("zone {} has {} entities outside its bounds",
                                   ZoneIdToString(zone.Id), outside),
        });
    }

    // Portal and transition rules. Portal scanning covers open zones only;
    // Doorway is the only topology that promises an aperture, so Seam and
    // Teleport transitions are exempt from every portal rule. Records append
    // in rule order, ascending source id within each rule.
    {
        struct PortalSighting
        {
            ZoneId Zone;
            bool HasBounds = false;
            Aabb3d Bounds;
        };
        std::unordered_map<uint64_t, std::vector<PortalSighting>> linkedPortals;
        std::vector<ContentRiskRecord> missing;
        std::vector<ContentRiskRecord> duplicate;
        std::vector<ContentRiskRecord> unverifiedRecords;
        std::vector<ContentRiskRecord> misaligned;
        std::vector<ContentRiskRecord> unlinked;
        std::vector<ContentRiskRecord> wrongZone;
        std::vector<ContentRiskRecord> brushMissing;

        const auto findTransition = [&](TransitionId id) -> const TransitionRecord*
        {
            for (const TransitionRecord& record : Manifest_.Transitions)
                if (record.Id == id)
                    return &record;
            return nullptr;
        };

        for (const ZoneHeader& zone : Manifest_.Zones)
        {
            const auto it = OpenZones_.find(zone.Id);
            if (it == OpenZones_.end())
                continue;
            const EditorScene& scene = it->second.Document->GetScene();
            size_t unlinkedCount = 0;
            size_t brushMissingCount = 0;
            for (EntityId entity : scene.GetAllEntities())
            {
                const PortalComponent* portal = scene.TryGetPortal(entity);
                if (portal == nullptr)
                    continue;
                const auto bounds = scene.TryGetWorldBounds(entity);
                if (!bounds.has_value())
                    ++brushMissingCount;
                const TransitionRecord* record =
                    portal->Transition.IsValid() ? findTransition(portal->Transition) : nullptr;
                if (record == nullptr)
                {
                    ++unlinkedCount;
                    continue;
                }
                linkedPortals[portal->Transition.Value].push_back(
                    { zone.Id, bounds.has_value(), bounds.value_or(Aabb3d{}) });
            }
            if (unlinkedCount > 0)
                unlinked.push_back({
                    .Severity = ContentRiskSeverity::Warning,
                    .Kind = ContentRiskSourceKind::Zone,
                    .SourceId = zone.Id.Value,
                    .RuleId = "partition.portal.unlinked",
                    .Message = std::format("zone {} has {} portals linked to no transition",
                                           ZoneIdToString(zone.Id), unlinkedCount),
                });
            if (brushMissingCount > 0)
                brushMissing.push_back({
                    .Severity = ContentRiskSeverity::Error,
                    .Kind = ContentRiskSourceKind::Zone,
                    .SourceId = zone.Id.Value,
                    .RuleId = "partition.portal.brush_missing",
                    .Message = std::format("zone {} has {} portal components on entities "
                                           "without brush geometry",
                                           ZoneIdToString(zone.Id), brushMissingCount),
                });
        }

        const std::vector<PortalSighting> noSightings;
        // A symmetric doorway pair is one physical opening: one linked portal
        // on either side satisfies the aperture check for BOTH directions. The
        // per-edge rules (duplicate, wrong_zone, misaligned) stay per-edge.
        const auto reverseOf = [&](const TransitionRecord& record) -> const TransitionRecord*
        {
            if (record.Flags.OneWay)
                return nullptr;
            for (const TransitionRecord& other : Manifest_.Transitions)
                if (&other != &record && other.From == record.To && other.To == record.From
                    && other.Topology == TransitionTopology::Doorway && !other.Flags.OneWay)
                    return &other;
            return nullptr;
        };
        const auto linkedInOwnZone = [&](const TransitionRecord& record)
        {
            const auto it = linkedPortals.find(record.Id.Value);
            if (it == linkedPortals.end())
                return false;
            for (const PortalSighting& sighting : it->second)
                if (sighting.Zone == record.From)
                    return true;
            return false;
        };

        for (const TransitionRecord& record : Manifest_.Transitions)
        {
            if (record.Topology != TransitionTopology::Doorway)
                continue;
            const auto sightingsIt = linkedPortals.find(record.Id.Value);
            const std::vector<PortalSighting>& sightings =
                sightingsIt != linkedPortals.end() ? sightingsIt->second : noSightings;

            size_t inFrom = 0;
            const PortalSighting* fromSighting = nullptr;
            bool anyWrongZone = false;
            for (const PortalSighting& sighting : sightings)
            {
                if (sighting.Zone == record.From)
                {
                    ++inFrom;
                    fromSighting = &sighting;
                }
                else
                {
                    anyWrongZone = true;
                }
            }

            const TransitionRecord* reverse = reverseOf(record);
            const bool pairSatisfied =
                inFrom > 0 || (reverse != nullptr && linkedInOwnZone(*reverse));
            const bool fromOpen = OpenZones_.contains(record.From);
            if (!fromOpen && !pairSatisfied)
                unverifiedRecords.push_back({
                    .Severity = ContentRiskSeverity::Unverified,
                    .Kind = ContentRiskSourceKind::Transition,
                    .SourceId = record.Id.Value,
                    .RuleId = "partition.transition.portal_unverified",
                    .Message = std::format("transition {} portal check needs zone {} loaded",
                                           TransitionIdToString(record.Id),
                                           ZoneIdToString(record.From)),
                });
            else if (fromOpen && !pairSatisfied)
                missing.push_back({
                    .Severity = ContentRiskSeverity::Warning,
                    .Kind = ContentRiskSourceKind::Transition,
                    .SourceId = record.Id.Value,
                    .RuleId = "partition.transition.portal_missing",
                    .Message = std::format("doorway transition {} has no linked portal in "
                                           "zone {}",
                                           TransitionIdToString(record.Id),
                                           ZoneIdToString(record.From)),
                });
            if (sightings.size() > 1)
                duplicate.push_back({
                    .Severity = ContentRiskSeverity::Error,
                    .Kind = ContentRiskSourceKind::Transition,
                    .SourceId = record.Id.Value,
                    .RuleId = "partition.transition.portal_duplicate",
                    .Message = std::format("transition {} is named by {} portals",
                                           TransitionIdToString(record.Id), sightings.size()),
                });
            if (anyWrongZone)
                wrongZone.push_back({
                    .Severity = ContentRiskSeverity::Error,
                    .Kind = ContentRiskSourceKind::Transition,
                    .SourceId = record.Id.Value,
                    .RuleId = "partition.portal.wrong_zone",
                    .Message = std::format("transition {} has a linked portal outside its "
                                           "source zone {}",
                                           TransitionIdToString(record.Id),
                                           ZoneIdToString(record.From)),
                });

            // Facing check on the one unambiguous case: a single portal with
            // bounds in the source zone. The thin axis must be the dominant
            // axis of the zone-center delta; the direction SIGN is not
            // checkable without a stored normal, which D9 forbids.
            if (fromOpen && inFrom == 1 && sightings.size() == 1 && fromSighting->HasBounds)
            {
                const ZoneHeader* fromHeader = FindZoneHeader(record.From);
                const ZoneHeader* toHeader = FindZoneHeader(record.To);
                if (fromHeader != nullptr && toHeader != nullptr)
                {
                    const Vec3d delta = toHeader->Bounds.Center() - fromHeader->Bounds.Center();
                    const double magnitudes[3] = { std::abs(static_cast<double>(delta[0])),
                                                   std::abs(static_cast<double>(delta[1])),
                                                   std::abs(static_cast<double>(delta[2])) };
                    constexpr double epsilon = 1e-6;
                    if (magnitudes[0] > epsilon || magnitudes[1] > epsilon
                        || magnitudes[2] > epsilon)
                    {
                        int deltaAxis = 0;
                        for (int i = 1; i < 3; ++i)
                            if (magnitudes[i] > magnitudes[deltaAxis])
                                deltaAxis = i;
                        if (DominantPortalAxis(fromSighting->Bounds) != deltaAxis)
                            misaligned.push_back({
                                .Severity = ContentRiskSeverity::Warning,
                                .Kind = ContentRiskSourceKind::Transition,
                                .SourceId = record.Id.Value,
                                .RuleId = "partition.transition.portal_misaligned",
                                .Message = std::format(
                                    "transition {} portal's thin axis does not face "
                                    "zone {}",
                                    TransitionIdToString(record.Id),
                                    ZoneIdToString(record.To)),
                            });
                    }
                }
            }
        }

        const auto appendSorted = [&](std::vector<ContentRiskRecord>& records)
        {
            std::sort(records.begin(), records.end(),
                      [](const ContentRiskRecord& a, const ContentRiskRecord& b)
                      { return a.SourceId < b.SourceId; });
            for (ContentRiskRecord& record : records)
                ValidationRecords_.push_back(std::move(record));
        };
        appendSorted(missing);
        appendSorted(duplicate);
        appendSorted(unverifiedRecords);
        appendSorted(misaligned);
        appendSorted(unlinked);
        appendSorted(wrongZone);
        appendSorted(brushMissing);
    }

    auto& log = Logging_.GetLogger<WorldDocument>();
    for (const ContentRiskRecord& record : ValidationRecords_)
    {
        if (record.Severity == ContentRiskSeverity::Error)
            log.Error("{}: {}", record.RuleId, record.Message);
    }
}

std::string WorldDocument::UserSidecarPath() const
{
    return WorldPath_ + ".user.json";
}

void WorldDocument::WriteUserSidecar() const
{
    if (!WorldMode_ || WorldPath_.empty())
        return;

    JsonValue::Object root;
    if (FocusZone_.IsValid())
        root.emplace_back("focus_zone", JsonValue{ ZoneIdToString(FocusZone_) });
    if (ViewSettings_ != nullptr)
    {
        root.emplace_back("show_zone_bounds", JsonValue{ ViewSettings_->ShowZoneBounds });
        root.emplace_back("streaming_preview", JsonValue{ ViewSettings_->StreamingPreview });
        root.emplace_back("preview_hop_count", JsonValue{ ViewSettings_->PreviewHopCount });
    }

    JsonValue::Array zones;
    for (const ZoneHeader& header : Manifest_.Zones)
    {
        const auto it = OpenZones_.find(header.Id);
        if (it == OpenZones_.end())
            continue;
        JsonValue::Object entry;
        entry.emplace_back("id", JsonValue{ ZoneIdToString(header.Id) });
        entry.emplace_back("open", JsonValue{ true });
        entry.emplace_back("visible", JsonValue{ it->second.View.VisibleInEditor });
        zones.emplace_back(JsonValue{ std::move(entry) });
    }
    root.emplace_back("zones", JsonValue{ std::move(zones) });

    std::ofstream file(UserSidecarPath(), std::ios::binary | std::ios::trunc);
    if (file.is_open())
        file << JsonStringify(JsonValue{ std::move(root) }, /*pretty*/ true);
}

ZoneId WorldDocument::ApplyUserSidecar()
{
    std::ifstream file(UserSidecarPath(), std::ios::binary);
    if (!file.is_open())
        return ZoneId{};
    std::ostringstream buffer;
    buffer << file.rdbuf();

    // A malformed or missing sidecar is silently ignored: it is per-user view
    // state, never content, so the defaults (start zone focused, nothing else
    // open) are always an acceptable outcome.
    const std::optional<JsonValue> root = JsonParse(buffer.str());
    if (!root.has_value() || !root->IsObject())
        return ZoneId{};

    if (const JsonValue* zones = root->Find("zones"); zones != nullptr && zones->IsArray())
    {
        for (const JsonValue& entry : zones->AsArray())
        {
            const JsonValue* id = entry.Find("id");
            if (id == nullptr || !id->IsString())
                continue;
            const auto zone = ZoneIdFromString(id->AsString());
            if (!zone.has_value() || FindZoneHeader(*zone) == nullptr)
                continue;
            const JsonValue* open = entry.Find("open");
            if (open == nullptr || !open->IsBool() || !open->AsBool())
                continue;
            if (!LoadZone(*zone))
                continue;
            if (const JsonValue* visible = entry.Find("visible");
                visible != nullptr && visible->IsBool())
                SetZoneVisible(*zone, visible->AsBool());
        }
    }

    if (const JsonValue* show = root->Find("show_zone_bounds");
        show != nullptr && show->IsBool() && ViewSettings_ != nullptr)
        ViewSettings_->ShowZoneBounds = show->AsBool();
    if (const JsonValue* preview = root->Find("streaming_preview");
        preview != nullptr && preview->IsBool() && ViewSettings_ != nullptr)
        ViewSettings_->StreamingPreview = preview->AsBool();
    if (const JsonValue* hops = root->Find("preview_hop_count");
        hops != nullptr && hops->IsNumber() && ViewSettings_ != nullptr)
        ViewSettings_->PreviewHopCount =
            std::max(0, static_cast<int>(hops->AsNumber()));

    if (const JsonValue* focus = root->Find("focus_zone"); focus != nullptr && focus->IsString())
    {
        if (const auto zone = ZoneIdFromString(focus->AsString()); zone.has_value())
            return *zone;
    }
    return ZoneId{};
}

void WorldDocument::CloseWorldToLegacy()
{
    WriteUserSidecar();
    WorldMode_ = false;
    WorldPath_.clear();
    Manifest_ = {};
    IndexDirty_ = true;
    WorldDirty_ = false;
    FocusZone_ = ZoneId{};
    OpenZones_.clear();
    ValidationRecords_.clear();
    LegacyDocument_ = std::make_unique<EditorDocument>(Logging_);
    if (Assets_)
        LegacyDocument_->SetAssetEnvironment(*Assets_);
}
